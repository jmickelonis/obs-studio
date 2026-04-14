#include "OBSDock.hpp"

#include <widgets/OBSBasic.hpp>

#include <QCheckBox>
#include <QMessageBox>
#include <QStylePainter>

#include "moc_OBSDock.cpp"

#ifdef _WIN32
#include <windowsx.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi")
#endif

TitleBarWidget::TitleBarWidget(OBSDock *dock) : QWidget(dock)
{
	// Don't cover up the dock widget
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_TransparentForMouseEvents);

	TitleBarLayout *layout = new TitleBarLayout(this);
	layout->setWidget(TitleBarLayout::FloatButton, dock->floatButton);
	layout->setWidget(TitleBarLayout::CloseButton, dock->closeButton);

	connect(dock, &QDockWidget::featuresChanged, this, &TitleBarWidget::onFeaturesChanged);
	connect(dock, &QDockWidget::topLevelChanged, this, &TitleBarWidget::onTopLevelChanged);
}

bool TitleBarWidget::event(QEvent *event)
{
	switch (event->type()) {

	case QEvent::ApplicationLayoutDirectionChange:
	case QEvent::LayoutDirectionChange:
	case QEvent::ParentChange:
	case QEvent::StyleChange:
		layout()->invalidate();
		break;

	default:
		break;
	}

	return QWidget::event(event);
}

OBSDock *TitleBarWidget::getDock() const
{
	return qobject_cast<OBSDock *>(parentWidget());
}

void TitleBarWidget::onFeaturesChanged(QDockWidget::DockWidgetFeatures)
{
	layout()->invalidate();
}

void TitleBarWidget::onTopLevelChanged(bool floating)
{
	layout()->invalidate();

	OBSDock *dock = getDock();
	dock->clearCursor();

#ifdef _WIN32
	if (dock->isFloating() && dock->mouseState == OBSDock::MouseState::NotPressed)
		dock->setDropShadow(true);
#endif

	// Stop from showing hover after setting floatable
	dock->floatButton->setAttribute(Qt::WA_UnderMouse, false);

	// Activate the window when [un]floating
	QWindow *window = this->window()->windowHandle();
	if (window)
		window->requestActivate();

	if (!floating)
		// Stock doesn't normally bring it to the top for some reason
		dock->raise();
}

TitleBarLayout::TitleBarLayout(QWidget *parent) : QLayout(parent), items(2, nullptr) {}

TitleBarLayout::~TitleBarLayout()
{
	qDeleteAll(items);
}

QWidget *TitleBarLayout::getWidget(Role role) const
{
	QLayoutItem *item = items.at(role);
	return item ? item->widget() : nullptr;
}

void TitleBarLayout::setWidget(Role role, QWidget *widget)
{
	QWidget *old = getWidget(role);

	if (old) {
		old->hide();
		removeWidget(old);
	}

	if (widget) {
		/* Do not add the button as a child,
		 * because it's already a child of the dock itself
		 */
		//addChildWidget(widget);

		items[role] = new QWidgetItemV2(widget);
		widget->show();
	} else {
		items[role] = nullptr;
	}

	invalidate();
}

QLayoutItem *TitleBarLayout::itemAt(int index) const
{
	int count = 0;
	for (QLayoutItem *item : items) {
		if (!item)
			continue;
		if (index == count++)
			return item;
	}
	return nullptr;
}

QLayoutItem *TitleBarLayout::takeAt(int index)
{
	int count = 0;
	for (int i = 0; i < items.count(); i++) {
		QLayoutItem *item = items.at(i);
		if (!item)
			continue;
		if (index == count++) {
			items[i] = nullptr;
			invalidate();
			return item;
		}
	}
	return nullptr;
}

int TitleBarLayout::count() const
{
	int count = 0;
	for (QLayoutItem *item : items)
		if (item)
			count++;
	return count;
}

QSize TitleBarLayout::sizeHint() const
{
	OBSDock *dock = getDock();

	bool floatable = dock->hasFeature(QDockWidget::DockWidgetFloatable);
	bool closable = dock->hasFeature(QDockWidget::DockWidgetClosable);

	QAbstractButton *floatButton = dock->floatButton;
	QAbstractButton *closeButton = dock->closeButton;

	/* Since the default updateButtons implementation can't be overridden,
	 * we can set button visibility here.
	 */
	floatButton->setVisible(floatable);
	closeButton->setVisible(closable);

	QSize floatSize = floatable ? floatButton->sizeHint() : QSize(0, 0);
	QSize closeSize = closable ? closeButton->sizeHint() : QSize(0, 0);

	bool vertical = dock->hasFeature(QDockWidget::DockWidgetVerticalTitleBar);
	int buttonHeight, w;

	if (vertical) {
		buttonHeight = qMax(floatSize.width(), closeSize.width());
		w = floatSize.height() + closeSize.height();
	} else {
		buttonHeight = qMax(floatSize.height(), closeSize.height());
		w = floatSize.width() + closeSize.width();
	}

	int margin = dock->style()->pixelMetric(QStyle::PM_DockWidgetTitleMargin, nullptr, dock);
	int h = qMax(dock->fontMetrics().height(), buttonHeight) + margin * 2;
	w += h + margin * 3;
	return vertical ? QSize(h, w) : QSize(w, h);
}

void TitleBarLayout::setGeometry(const QRect &)
{
	OBSDock *dock = getDock();

	QStyleOptionDockWidget opt;
	dock->initStyleOption(&opt);

	QStyle *style = dock->style();
	QRect rect = style->subElementRect(QStyle::SE_DockWidgetCloseButton, &opt, dock);
	if (!rect.isNull()) {
		QAbstractButton *button = dock->closeButton;
		/*
         * Force the button sizes before setting the geometry.
         * For some reason, normally the buttons end up larger than returned here,
         * so they overlap or fall outside the title bar bounds.
         */
		button->setFixedSize(rect.size());
		button->setGeometry(rect);
	}
	rect = style->subElementRect(QStyle::SE_DockWidgetFloatButton, &opt, dock);
	if (!rect.isNull()) {
		QAbstractButton *button = dock->floatButton;
		button->setFixedSize(rect.size());
		button->setGeometry(rect);
	}
}

QSize TitleBarLayout::minimumSize() const
{
	return sizeHint();
}

OBSDock *TitleBarLayout::getDock() const
{
	return qobject_cast<OBSDock *>(parentWidget()->parentWidget());
}

OBSDock::OBSDock(const QString &title, QWidget *parent) : QDockWidget(title, parent)
{
#ifndef _WIN32
	setAttribute(Qt::WA_TranslucentBackground);
#endif

	cursor = Qt::BlankCursor;
	mouseState = NotPressed;
	settingFlags = false;

	// Get the dock's buttons (hopefully these will never be null!)
	floatButton = findChild<QAbstractButton *>("qt_dockwidget_floatbutton");
	closeButton = findChild<QAbstractButton *>("qt_dockwidget_closebutton");

	TitleBarWidget *titleBar = new TitleBarWidget(this);
	setTitleBarWidget(titleBar);

#ifdef __QT_SUPPORTS_SYSTEM_RESIZE
	// Go scorched-Earth to remove the stock resizer
	QList<QObject *> objects = findChildren<QObject *>();
	for (QObject *object : objects)
		if (!qobject_cast<QWidget *>(object))
			removeEventFilter(object);
#endif

	connect(this, &QDockWidget::visibilityChanged, this, &OBSDock::onVisibilityChanged);
}

bool OBSDock::hasFeature(QDockWidget::DockWidgetFeature feature)
{
	return features() & feature;
}

void OBSDock::setVisible(bool visible)
{
	if (!settingFlags) {
		/* Remove the bypass flag that the base class sets.
		 * This way, all drags act the same, and transparency works.
		 */
		Qt::WindowFlags flags = windowFlags();
		Qt::WindowFlags newFlags = flags & ~Qt::BypassWindowManagerHint;
		if (newFlags != flags) {
			settingFlags = true;
			setWindowFlags(newFlags);
			settingFlags = false;
		}
	}

	QDockWidget::setVisible(visible);
}

void OBSDock::closeEvent(QCloseEvent *event)
{
	auto msgBox = []() {
		QMessageBox msgbox(App()->GetMainWindow());
		msgbox.setWindowTitle(QTStr("DockCloseWarning.Title"));
		msgbox.setText(QTStr("DockCloseWarning.Text"));
		msgbox.setIcon(QMessageBox::Icon::Information);
		msgbox.addButton(QMessageBox::Ok);

		QCheckBox *cb = new QCheckBox(QTStr("DoNotShowAgain"));
		msgbox.setCheckBox(cb);

		msgbox.exec();

		if (cb->isChecked()) {
			config_set_bool(App()->GetUserConfig(), "General", "WarnedAboutClosingDocks", true);
			config_save_safe(App()->GetUserConfig(), "tmp", nullptr);
		}
	};

	bool warned = config_get_bool(App()->GetUserConfig(), "General", "WarnedAboutClosingDocks");
	if (!OBSBasic::Get()->isClosing() && !warned) {
		QMetaObject::invokeMethod(App(), "Exec", Qt::QueuedConnection, Q_ARG(VoidFunc, msgBox));
	}

	QDockWidget::closeEvent(event);

	if (widget() && event->isAccepted()) {
		QEvent widgetEvent(QEvent::Type(QEvent::User + QEvent::Close));
		qApp->sendEvent(widget(), &widgetEvent);
	}
}

void OBSDock::showEvent(QShowEvent *event)
{
	QDockWidget::showEvent(event);
}

bool OBSDock::event(QEvent *e)
{
	switch (e->type()) {

#ifdef __QT_SUPPORTS_SYSTEM_RESIZE
	case QEvent::ChildAdded: {
		QChildEvent *childEvent = static_cast<QChildEvent *>(e);
		QObject *child = childEvent->child();
		if (child != this && !qobject_cast<QWidget *>(child)) {
			/* Might be a resizer.
			 * Kill it!
			 */
			QTimer::singleShot(1, this, [this, child]() { removeEventFilter(child); });
		}
		break;
	}
#endif

	case QEvent::ContextMenu: {
		if (mouseState != NotPressed)
			break;

		QContextMenuEvent *contextMenuEvent = static_cast<QContextMenuEvent *>(e);
		if (!isOverTitleBar(contextMenuEvent->pos()))
			break;

		if (isFloating() && OBSApp::IsWayland())
			/* Wayland can't accurately get the global mouse position here.
			 * Just disable the context menu when floating.
			 */
			return false;

		clearCursor();
		QMenu *menu = App()->GetMainWindow()->findChild<QMenu *>("menuDocks");
		menu->exec(contextMenuEvent->globalPos());
		return true;
	}

	case QEvent::WindowActivate:
	case QEvent::WindowDeactivate:
		if (isFloating())
			// Update the window border
			update();
		break;

	case QEvent::HoverEnter:
	case QEvent::HoverMove: {
		if (mouseState != NotPressed)
			break;

		QHoverEvent *hoverEvent = static_cast<QHoverEvent *>(e);
		updateCursor(hoverEvent->position().toPoint());
		break;
	}

	case QEvent::HoverLeave:
		if (mouseState != Dragging)
			clearCursor();
		break;

	case QEvent::MouseButtonPress: {
		if (mouseState != NotPressed)
			break;

		QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(e);
		if (mouseEvent->button() != Qt::LeftButton)
			break;

		pressPosition = mouseEvent->pos();

#ifdef __QT_SUPPORTS_SYSTEM_RESIZE
		pressEdges = getResizeEdges(pressPosition);
		if (pressEdges) {
			// Will do a system resize on drag
			mouseState = Pressed;
			return true;
		}
#endif

		if (!isOverTitleBar(pressPosition))
			break;

		if (isFloating() && ((mouseEvent->modifiers() & Qt::ControlModifier) ||
				     !hasFeature(QDockWidget::DockWidgetFloatable))) {
			// Will do a system move on drag
			mouseState = CtrlPressed;
			updateCursor(pressPosition);
			return true;
		}

		// Stock implementation will handle a drag
		mouseState = Pressed;
		updateCursor(pressPosition);
		break;
	}

	case QEvent::Move:
		if (mouseState == Pressed && mouseGrabber() && isFloating()) {
			// Make the window transparent when we're dragging
			mouseState = Dragging;
			setTranslucent(true);

#ifdef _WIN32
			/* Disable the drop shadow when moving the dock around.
			 * This prevents a lot of glitches (like when moving between screens).
			 */
			setDropShadow(false);
#endif
		}
		break;

	case QEvent::MouseMove: {
		if (mouseState == NotPressed)
			// Works around strange bug where dock pops out temporarily
			return true;

		if (mouseState == CtrlPressed) {
			QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(e);
			if (!shouldStartDrag(mouseEvent))
				return false;

#ifdef _WIN32
			setDropShadow(false);
#endif

			mouseState = OBSApp::IsWayland() ? NotPressed : CtrlDragging;
			window()->windowHandle()->startSystemMove();
			return true;
		}

#ifdef __QT_SUPPORTS_SYSTEM_RESIZE
		if (mouseState != Pressed || !pressEdges)
			break;

		QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(e);
		if (!shouldStartDrag(mouseEvent))
			return false;

#ifdef _WIN32
		setDropShadow(false);
#endif

		mouseState = OBSApp::IsWayland() ? NotPressed : Resizing;
		window()->windowHandle()->startSystemResize(pressEdges);
		return true;
#endif

		break;
	}

	case QEvent::MouseButtonRelease: {
		if (mouseState == NotPressed)
			break;

		QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(e);
		Qt::MouseButton button = mouseEvent->button();

		if (mouseState == Pressed && OBSApp::IsWayland() && isFloating() && button <= Qt::LeftButton) {
			// Wayland sends this after a floating drag

			if (mouseGrabber())
				releaseMouse();

			temporarilyDisableAnimations();
			mouseState = NotPressed;
			updateCursor(mouseEvent->pos());
			break;
		}

		if (button != Qt::LeftButton)
			break;

		if (mouseState == Dragging) {
			setTranslucent(false);
			temporarilyDisableAnimations();
#ifdef _WIN32
			setDropShadow(true);
			fixBounds();
#endif
		}
#ifndef _WIN32
		else if (mouseState == CtrlDragging || mouseState == Resizing) {
			fixBounds();
		}
#endif

		mouseState = NotPressed;
		updateCursor(mouseEvent->pos());
		break;
	}

	default:
		break;
	}

	return QDockWidget::event(e);
}

void OBSDock::paintEvent(QPaintEvent *)
{
	QStylePainter painter(this);

	if (isFloating()) {
		QStyleOptionFrame opt;
		opt.initFrom(this);
		painter.drawPrimitive(QStyle::PE_FrameDockWidget, opt);
	}

	QStyleOptionDockWidget opt;
	initStyleOption(&opt);
	const QFont font = this->font();
	if (font == QApplication::font("QDockWidget")) {
		opt.fontMetrics = QFontMetrics(font);
		painter.setFont(font);
	}
	painter.drawControl(QStyle::CE_DockWidgetTitle, opt);
}

#ifdef _WIN32
bool OBSDock::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
	if (!isFloating())
		return false;

	MSG *msg = reinterpret_cast<MSG *>(message);

	switch (msg->message) {

	case WM_SHOWWINDOW: {
		HWND handle = (HWND)winId();

		// Don't allow rounded corners
		DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_DONOTROUND;
		DwmSetWindowAttribute(handle, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

		break;
	}

	case WM_SIZING: {
		// Notifies us that we're about to resize,
		// allowing us to prevent illegal resize operations

		WPARAM edge = msg->wParam;
		RECT &rect = *reinterpret_cast<RECT *>(msg->lParam);

		HMONITOR monitor = MonitorFromWindow(HWND(winId()), MONITOR_DEFAULTTONEAREST);
		MONITORINFO monitorInfo;
		monitorInfo.cbSize = sizeof(MONITORINFO);
		GetMonitorInfo(monitor, &monitorInfo);
		RECT &screenBounds = monitorInfo.rcMonitor;

		int xMin = screenBounds.left;
		int yMin = screenBounds.top;

		if (edge == WMSZ_TOPLEFT || edge == WMSZ_LEFT || edge == WMSZ_BOTTOMLEFT)
			if (rect.left < xMin)
				// Don't let the left border be dragged out of bounds
				rect.left = xMin;

		if (edge == WMSZ_TOPLEFT || edge == WMSZ_TOP || edge == WMSZ_TOPRIGHT) {
			if (rect.top < yMin) {
				// Don't let the top border be dragged out of bounds
				rect.top = yMin;
			} else {
				// Keep the title bar completely on screen
				TitleBarWidget *titleBar = findChild<TitleBarWidget *>();
				int yMax = screenBounds.bottom -
					   (int)((titleBar->y() + titleBar->height()) * devicePixelRatioF());
				if (rect.top > yMax)
					rect.top = yMax;
			}
		}

		break;
	}

	case WM_EXITSIZEMOVE:
		mouseState = NotPressed;
		fixBounds();
		setDropShadow(true);
		setTranslucent(false);
		break;

	default:
		break;
	}

	return false;
}
#endif

Qt::Edges OBSDock::getResizeEdges(const QPoint &position)
{
	Qt::Edges edges;

	if (!isFloating())
		return edges;

	const int x = position.x();
	const int y = position.y();
	const int w = width();
	const int h = height();

	if (x < 0 || x >= w || y < 0 || y >= h)
		// Position is not within this window
		return edges;

	/* Try to match the default implementation.
	 * A little extra space is given inside the title bar.
	 */
	const QWidget *titleBar = titleBarWidget();
	bool inTitleBar = y < titleBar->y() + titleBar->height();
	static int borderSize = 4;

	if (x < borderSize)
		edges |= Qt::LeftEdge;
	else if (x >= w - (inTitleBar ? borderSize - 1 : borderSize))
		edges |= Qt::RightEdge;

	if (y < borderSize)
		edges |= Qt::TopEdge;
	else if (y >= h - borderSize)
		edges |= Qt::BottomEdge;

	return edges;
}

Qt::CursorShape OBSDock::getCursor(const QPoint &position)
{
	if (floatButton->underMouse() || closeButton->underMouse())
		return Qt::BlankCursor;

	if (isFloating()) {
		Qt::Edges edges = getResizeEdges(position);

		if (edges & Qt::LeftEdge)
			return edges & Qt::TopEdge      ? Qt::SizeFDiagCursor
			       : edges & Qt::BottomEdge ? Qt::SizeBDiagCursor
							: Qt::SizeHorCursor;
		else if (edges & Qt::RightEdge)
			return edges & Qt::TopEdge      ? Qt::SizeBDiagCursor
			       : edges & Qt::BottomEdge ? Qt::SizeFDiagCursor
							: Qt::SizeHorCursor;
		else if (edges & (Qt::TopEdge | Qt::BottomEdge))
			return Qt::SizeVerCursor;
	}

	switch (mouseState) {
	case MouseState::Pressed:
	case MouseState::CtrlPressed:
		return Qt::ClosedHandCursor;
	default:
		// return (isFloating() || hasFeature(QDockWidget::DockWidgetMovable)) && isOverTitleBar(position)
		// 	       ? Qt::OpenHandCursor
		// 	       : Qt::BlankCursor;
		return Qt::BlankCursor;
	}
}

void OBSDock::updateCursor(const QPoint &position)
{
	updateCursor(getCursor(position));
}

void OBSDock::updateCursor(Qt::CursorShape cursor)
{
	OBSApp *app = App();
	if (cursor != Qt::BlankCursor) {
		if (this->cursor != Qt::BlankCursor)
			app->changeOverrideCursor(cursor);
		else
			app->setOverrideCursor(cursor);
	} else if (this->cursor != Qt::BlankCursor) {
		app->restoreOverrideCursor();
	}
	this->cursor = cursor;
}

void OBSDock::clearCursor()
{
	updateCursor(Qt::BlankCursor);
}

#ifdef _WIN32
void OBSDock::setDropShadow(bool value)
{
	HWND hwnd = (HWND)winId();
	DWORD style = GetWindowLong(hwnd, GWL_STYLE);
	auto flags = WS_CAPTION | WS_CLIPCHILDREN;

	if (value) {
		style |= flags;
		const MARGINS margins = {-1};
		DwmExtendFrameIntoClientArea(hwnd, &margins);
	} else {
		style &= ~flags;
	}

	SetWindowLong(hwnd, GWL_STYLE, style);
}
#endif

void OBSDock::setTranslucent(bool value)
{
	setWindowOpacity(value ? .8 : 1);
}

/* Moves the dock back onto the screen if it's too far off of it.
 */
void OBSDock::fixBounds()
{
	int x = this->x();
	int y = this->y();

	QScreen *screen = window()->windowHandle()->screen();
	QRect screenBounds = screen->availableGeometry();
	int xMin = screenBounds.left();
	int yMin = screenBounds.top();

	if (x < xMin) {
		x = xMin;
	} else {
		int xMax = screenBounds.right() - this->width() + 1;
		if (x > xMax)
			x = xMax;
	}

	if (y < yMin) {
		y = yMin;
	} else {
		TitleBarWidget *titleBar = findChild<TitleBarWidget *>();
		int yMax = screenBounds.bottom() - (titleBar->y() + titleBar->height()) + 1;
		if (y > yMax)
			y = yMax;
	}

	move(x, y);
}

bool OBSDock::isOverTitleBar(const QPoint &point)
{
	TitleBarWidget *titleBar = findChild<TitleBarWidget *>();
	QPoint titleBarPosition = titleBar->mapFrom(this, point);
	return titleBar->rect().contains(titleBarPosition);
}

bool OBSDock::shouldStartDrag(QMouseEvent *event)
{
	return (event->pos() - pressPosition).manhattanLength() >= QApplication::startDragDistance();
}

/* Disable animations temporarily when docking, to make things look snappier.
 * This immediately snaps docks into place
 * (animations only occur while dragging/re-positioning).
 */
void OBSDock::temporarilyDisableAnimations()
{
	QMainWindow *mainWindow = App()->GetMainWindow();
	mainWindow->setAnimated(false);
	QTimer::singleShot(1, this, [mainWindow]() { mainWindow->setAnimated(true); });
}

void OBSDock::onVisibilityChanged(bool visible)
{
	if (visible && !isFloating())
		// This fixes browser docks disappearing when tabbed
		raise();

	/* This fixes the button layout being wrong for already-floating docks
	 * (on Windows at the very least)
	 */
	TitleBarWidget *titleBar = findChild<TitleBarWidget *>();
	titleBar->layout()->invalidate();
}
