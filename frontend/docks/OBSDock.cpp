#include "OBSDock.hpp"

#include <widgets/OBSBasic.hpp>

#include <QCheckBox>
#include <QMessageBox>
#include <QStylePainter>

#include "moc_OBSDock.cpp"

#ifdef _WIN32
#include "../OBSWin32.hpp"
#endif

TitleBarWidget::TitleBarWidget(OBSDock *dock) : QWidget(dock)
{
	// Don't cover up the dock widget
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_TransparentForMouseEvents);

	TitleBarLayout *layout = new TitleBarLayout(this);
	setLayout(layout);

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
		dock->setDropShadowInternal(dock->dropShadow);
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

void TitleBarLayout::setGeometry(const QRect &geometry)
{
	OBSDock *dock = getDock();

	QStyleOptionDockWidget opt;
	dock->initStyleOption(&opt);

	QStyle *style = dock->style();
	int frameThickness = dock->isFloating() ? style->pixelMetric(QStyle::PM_DockWidgetFrameWidth, nullptr, dock)
						: 0;

	auto handleButton = [&](QAbstractButton *button, QStyle::SubElement subElement) {
		QRect rect = style->subElementRect(subElement, &opt, dock);
		if (rect.isNull())
			return;
		int w = rect.width();
		int h = rect.height();
		/*
         * Force the button sizes before setting the geometry.
         * For some reason, normally the buttons end up larger than returned here,
         * so they overlap or fall outside the title bar bounds.
         */
		button->setFixedSize(w, h);
		// Fix vertical centering
		rect.setY(frameThickness + (geometry.height() - h) / 2);
		button->setGeometry(rect);
	};

	handleButton(dock->closeButton, QStyle::SE_DockWidgetCloseButton);
	handleButton(dock->floatButton, QStyle::SE_DockWidgetFloatButton);
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

#ifdef _WIN32
	dropShadow = true;
#endif

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

bool OBSDock::isDropShadow()
{
#ifdef _WIN32
	return dropShadow;
#else
	return false;
#endif
}

void OBSDock::setDropShadow(bool value)
{
#ifdef _WIN32
	bool old = dropShadow;
	if (value == old)
		return;
	dropShadow = value;
	if (isFloating())
		setDropShadowInternal(value);
#endif
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

#ifdef __QT_SUPPORTS_SYSTEM_RESIZE
bool OBSDock::eventFilter(QObject *watched, QEvent *event)
{
	if (!isFloating() || watched != window()->windowHandle())
		goto END;

	/* Filter events going to the floating window.
	 * This allows us to better support native resizing.
	 */

	switch (event->type()) {

	case QEvent::Enter: {
		if (mouseState != NotPressed)
			break;

		// Update the edges and cursor
		QEnterEvent *enterEvent = static_cast<QEnterEvent *>(event);
		const QPoint &pos = enterEvent->position().toPoint();
		edges = getResizeEdges(pos);
		updateCursor(pos);

		if (edges)
			// Don't forward to the widget if we can resize
			return true;

		break;
	}

	case QEvent::MouseButtonPress: {
		if (mouseState != NotPressed || !edges)
			break;

		QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
		if (mouseEvent->button() != Qt::LeftButton)
			return true;

		/* An edge was pressed.
		 * We'll do a system resize when dragged far enough.
		 */
		pressPosition = mouseEvent->pos();
		mouseState = Pressed;
		return true;
	}

	case QEvent::MouseButtonDblClick: {
		if (!edges)
			break;

		// Ignore double-clicks on the edges
		return true;
	}

	case QEvent::MouseMove: {
		if (mouseState == Pressed && edges) {
			QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
			if (!shouldStartDrag(mouseEvent))
				return true;

			// Dragged an edge far enough to start a system resize

#ifdef _WIN32
			setDropShadowInternal(false);
#endif
			mouseState = OBSApp::IsWayland() ? NotPressed : Resizing;
			window()->windowHandle()->startSystemResize(edges);
			return true;
		}

		if (mouseState != NotPressed)
			break;

		// Update the edges and cursor
		QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
		const QPoint &pos = mouseEvent->pos();
		Qt::Edges oldEdges = edges;
		edges = getResizeEdges(pos);
		updateCursor(pos);

		if (edges == oldEdges) {
			if (edges)
				return true;

			break;
		}

		if (edges) {
			/* Went from not being over an edge to being over one.
			 * If any widgets are showing hover state,
			 * they need to be forced out of it.
			 */
			QWidget *widget = QApplication::widgetAt(mouseEvent->globalPosition().toPoint());
			while (widget && widget != this) {
				QEvent leaveEvent(QEvent::Leave);
				QApplication::sendEvent(widget, &leaveEvent);
				widget = widget->parentWidget();
			}

			return true;
		} else {
			// We already ate the enter event, so we have to send a new one
			const QPointF pos = mouseEvent->position();
			QEnterEvent enterEvent(pos, pos, mouseEvent->globalPosition());
			QApplication::sendEvent(window()->windowHandle(), &enterEvent);

			break;
		}
	}

	case QEvent::MouseButtonRelease: {
		if (!edges)
			break;

		QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(event);
		if (mouseEvent->button() != Qt::LeftButton)
			return true;

		if (mouseState == Pressed) {
			mouseState = NotPressed;
			return true;
		}

		if (mouseState != Resizing)
			return true;

		// Done resizing
		mouseState = NotPressed;
		edges = Qt::Edges();
		clearCursor();

		/* Send an enter event to the window at the current location.
		 * Otherwise, we'd have to move the mouse again to show hover state.
		 */
		const QPointF pos = mouseEvent->position();
		QEnterEvent enterEvent(pos, pos, mouseEvent->globalPosition());
		QApplication::sendEvent(window()->windowHandle(), &enterEvent);

		return true;
	}

	case QEvent::Leave: {
		if (mouseState != NotPressed)
			break;
		edges = Qt::Edges();
		break;
	}
	}

END:
	return QDockWidget::eventFilter(watched, event);
}
#endif

bool OBSDock::event(QEvent *e)
{
	switch (e->type()) {

#ifdef __QT_SUPPORTS_SYSTEM_RESIZE

	case QEvent::Show:
		if (!isFloating())
			break;
		edges = Qt::Edges();
		window()->windowHandle()->installEventFilter(this);
		break;

	case QEvent::ChildAdded: {
		QChildEvent *childEvent = static_cast<QChildEvent *>(e);
		QObject *child = childEvent->child();
		if (child != this && !qobject_cast<QWidget *>(child)) {
			/* Might be a resizer.
			 * It's used to handle the default (non-system) window resize.
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

		if (!isOverTitleBar(pressPosition))
			break;

		if (isFloating() && ((mouseEvent->modifiers() & Qt::ControlModifier) ||
				     !hasFeature(QDockWidget::DockWidgetFloatable))) {
			// Will do a system move on drag
			mouseState = CtrlPressed;
			updateCursor(pressPosition);
			return true;
		}

		if (!hasFeature(QDockWidget::DockWidgetMovable))
			return false;

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
			setDropShadowInternal(false);
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
			setDropShadowInternal(false);
#endif

			mouseState = OBSApp::IsWayland() ? NotPressed : CtrlDragging;
			window()->windowHandle()->startSystemMove();
			return true;
		}

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
			setDropShadowInternal(dropShadow);
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
		painter.drawPrimitive(QStyle::PE_Widget, opt);
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

	case WM_NCACTIVATE:
		/* Browser docks can mess up normal [de]activate events,
		 * so go ahead and update from here */
		update();
		break;

	case WM_SHOWWINDOW: {
		HWND wnd = (HWND)winId();

		// Make the background transparent
		Win32::setBlurBehind(wnd, true);

		if (Win32::is11OrNewer())
			// Disable rounded corners
			Win32::setCornerPreference(wnd, DWMWCP_DONOTROUND);
		else
			// Enables drop shadows when WS_CAPTION is set
			Win32::enableSheetOfGlass(wnd);

		setDropShadowInternal(dropShadow);
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
		setDropShadowInternal(dropShadow);
		setTranslucent(false);
		break;

	default:
		break;
	}

	return false;
}
#endif

#ifdef __QT_SUPPORTS_SYSTEM_RESIZE
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

	// Give more room on the top/bottom edges, as well as inside the title bar
	static int borderSize = 5;
	static int borderSizeSmall = 2;

	int hBorderSize = borderSizeSmall;

	if (y < borderSize) {
		edges |= Qt::TopEdge;
		hBorderSize = borderSize;
	} else if (y >= h - borderSize) {
		edges |= Qt::BottomEdge;
		hBorderSize = borderSize;
	} else {
		const QWidget *titleBar = titleBarWidget();
		if (y < titleBar->y() + titleBar->height())
			hBorderSize = borderSize;
	}

	if (x < hBorderSize)
		edges |= Qt::LeftEdge;
	else if (x >= w - hBorderSize)
		edges |= Qt::RightEdge;

	return edges;
}
#endif

Qt::CursorShape OBSDock::getCursor(const QPoint &position)
{
	if (floatButton->underMouse() || closeButton->underMouse())
		return Qt::BlankCursor;

#ifdef __QT_SUPPORTS_SYSTEM_RESIZE
	if (isFloating()) {
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
#endif

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
void OBSDock::setDropShadowInternal(bool value)
{
	HWND wnd = (HWND)winId();

	if (Win32::is11OrNewer()) {
		/* Use small rounded corners, which gives us shadows for free,
		 * and doesn't leave any kind of obvious artifacts.
		 * The UI/theme has to account for the rounded corners, or risks being cut off.
		 */
		Win32::setCornerPreference(wnd, value ? DWMWCP_ROUNDSMALL : DWMWCP_DONOTROUND);
	} else {
		LONG style = Win32::getStyle(wnd);
		LONG flags = WS_CAPTION | WS_CLIPCHILDREN;
		if (value)
			style |= flags;
		else
			style &= ~flags;
		Win32::setStyle(wnd, style);
	}
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
