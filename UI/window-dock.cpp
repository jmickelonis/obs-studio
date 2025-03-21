#include "window-dock.hpp"
#include "obs-app.hpp"
#include "window-basic-main.hpp"

#include <QMessageBox>
#include <QCheckBox>

#include <QMenu>
#include <QScreen>
#include <QStyle>
#include <QStyleOptionFrame>
#include <QStylePainter>
#include <QTimer>
#include <QWindow>

#ifdef _WIN32
#include <windowsx.h>
#include <dwmapi.h>
#pragma comment(lib, "Dwmapi.lib")
#endif

#if defined(_WIN32) || defined(__linux__)
// Mac doesn't support this yet
#define __SUPPORTS_SYSTEM_RESIZE
#endif

TitleBarButton::TitleBarButton(QAbstractButton *stockButton) : stockButton(stockButton)
{
	setFocusPolicy(Qt::NoFocus);
}

TitleBarButton::~TitleBarButton()
{
	if (iconSize) {
		delete iconSize;
		iconSize = nullptr;
	}
}

QSize TitleBarButton::minimumSizeHint() const
{
	return sizeHint();
}

QSize TitleBarButton::sizeHint() const
{
	ensurePolished();

	int size =
		2 * stockButton->style()->pixelMetric(QStyle::PM_DockWidgetTitleBarButtonMargin, nullptr, stockButton);

	const QIcon icon = this->icon();
	if (!icon.isNull()) {
		const QSize iconSize = icon.actualSize(getIconSize());
		size += qMax(iconSize.width(), iconSize.height());
	}

	return QSize(size, size);
}

bool TitleBarButton::event(QEvent *event)
{
	switch (event->type()) {

	case QEvent::ScreenChangeInternal:
	case QEvent::StyleChange:
		if (iconSize) {
			delete iconSize;
			iconSize = nullptr;
		}
		break;

	case QEvent::Enter:
	case QEvent::Leave:
		if (isEnabled())
			update();
		break;

	default:
		break;
	}

	return QAbstractButton::event(event);
}

void TitleBarButton::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	QStyle *style = stockButton->style();

	QStyleOptionToolButton opt;
	opt.initFrom(this);
	opt.state |= QStyle::State_AutoRaise;

	if (style->styleHint(QStyle::SH_DockWidget_ButtonsHaveFrame, nullptr, stockButton)) {
		if (isEnabled() && underMouse() && !isChecked() && !isDown())
			opt.state |= QStyle::State_Raised;
		if (isChecked())
			opt.state |= QStyle::State_On;
		if (isDown())
			opt.state |= QStyle::State_Sunken;
		style->drawPrimitive(QStyle::PE_PanelButtonTool, &opt, &painter, stockButton);
	}

	opt.icon = icon();
	opt.features = QStyleOptionToolButton::None;
	opt.arrowType = Qt::NoArrow;
	opt.iconSize = getIconSize();
	style->drawComplexControl(QStyle::CC_ToolButton, &opt, &painter, stockButton);
}

QSize &TitleBarButton::getIconSize() const
{
	if (!iconSize) {
		int n = stockButton->style()->pixelMetric(QStyle::PM_SmallIconSize, nullptr, stockButton);
		iconSize = new QSize(n, n);
	}
	return *iconSize;
}

TitleBarLayout::TitleBarLayout(QWidget *parent) : QLayout(parent), items(RoleCount, nullptr) {}

TitleBarLayout::~TitleBarLayout()
{
	qDeleteAll(items);
}

QWidget *TitleBarLayout::getWidgetForRole(Role role) const
{
	QLayoutItem *item = items.at(role);
	return item ? item->widget() : nullptr;
}

void TitleBarLayout::setWidgetForRole(Role role, QWidget *widget)
{
	QWidget *old = getWidgetForRole(role);

	if (old) {
		old->hide();
		removeWidget(old);
	}

	if (widget) {
		addChildWidget(widget);
		items[role] = new QWidgetItemV2(widget);
		widget->show();
	} else {
		items[role] = nullptr;
	}

	invalidate();
}

int TitleBarLayout::count() const
{
	int result = 0;
	for (QLayoutItem *item : items)
		if (item)
			result++;
	return result;
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

QSize TitleBarLayout::sizeHint() const
{
	OBSDock *dock = getDock();
	QStyle *style = dock->style();

	QSize closeSize = dock->hasFeature(QDockWidget::DockWidgetClosable) ? items[CloseButton]->widget()->sizeHint()
									    : QSize(0, 0);

	QSize floatSize = dock->hasFeature(QDockWidget::DockWidgetFloatable) ? items[FloatButton]->widget()->sizeHint()
									     : QSize(0, 0);

	bool vertical = dock->hasFeature(QDockWidget::DockWidgetVerticalTitleBar);

	int buttonHeight = vertical ? qMax(closeSize.width(), floatSize.width())
				    : qMax(closeSize.height(), floatSize.height());
	int margin = style->pixelMetric(QStyle::PM_DockWidgetTitleMargin, nullptr, dock);

	int h = qMax(buttonHeight, dock->fontMetrics().height()) + 2 * margin;
	int w = (vertical ? closeSize.height() : closeSize.width()) +
		(vertical ? floatSize.height() : floatSize.width()) + h + 3 * margin;
	return vertical ? QSize(h, w) : QSize(w, h);
}

QSize TitleBarLayout::minimumSize() const
{
	return sizeHint();
}

QSize TitleBarLayout::maximumSize() const
{
	return parentWidget()->maximumSize();
}

void TitleBarLayout::setGeometry(const QRect &)
{
	OBSDock *dock = getDock();
	QStyle *style = dock->style();

	QStyleOptionDockWidget opt;
	dock->initStyleOption(&opt);

	QLayoutItem *item = items[CloseButton];
	QRect rect = style->subElementRect(QStyle::SE_DockWidgetCloseButton, &opt, dock);
	if (!rect.isNull())
		item->setGeometry(rect);

	item = items[FloatButton];
	rect = style->subElementRect(QStyle::SE_DockWidgetFloatButton, &opt, dock);
	if (!rect.isNull())
		item->setGeometry(rect);
}

TitleBarWidget::TitleBarWidget(OBSDock *dock) : QWidget(dock)
{
	// Don't cover up the dock widget
	setAttribute(Qt::WA_NoSystemBackground);

	// Hopefully these will never be null!
	closeButton = new TitleBarButton(dock->findChild<QAbstractButton *>("qt_dockwidget_closebutton"));
	floatButton = new TitleBarButton(dock->findChild<QAbstractButton *>("qt_dockwidget_floatbutton"));

	TitleBarLayout *layout = new TitleBarLayout(this);
	layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
	layout->setWidgetForRole(TitleBarLayout::FloatButton, floatButton);
	layout->setWidgetForRole(TitleBarLayout::CloseButton, closeButton);

	updateButtons();

	connect(closeButton, &QAbstractButton::clicked, this, &TitleBarWidget::onCloseClicked);
	connect(floatButton, &QAbstractButton::clicked, this, &TitleBarWidget::onFloatClicked);
	connect(dock, &QDockWidget::featuresChanged, this, &TitleBarWidget::onFeaturesChanged);
	connect(dock, &QDockWidget::topLevelChanged, this, &TitleBarWidget::onTopLevelChanged);
}

TitleBarWidget::~TitleBarWidget()
{
	delete closeButton;
	delete floatButton;
}

bool TitleBarWidget::event(QEvent *event)
{
	switch (event->type()) {

	case QEvent::ApplicationLayoutDirectionChange:
	case QEvent::LayoutDirectionChange:
	case QEvent::StyleChange:
	case QEvent::ParentChange:
		updateButtons();
		break;

	default:
		break;
	}

	return QWidget::event(event);
}

void TitleBarWidget::updateButtons()
{
	OBSDock *dock = getDock();
	QStyle *style = dock->style();

	QStyleOptionDockWidget opt;
	dock->initStyleOption(&opt);

	floatButton->setIcon(style->standardIcon(QStyle::SP_TitleBarNormalButton, &opt, dock));
	floatButton->setVisible(dock->hasFeature(QDockWidget::DockWidgetFloatable));

	closeButton->setIcon(style->standardIcon(QStyle::SP_TitleBarCloseButton, &opt, dock));
	closeButton->setVisible(dock->hasFeature(QDockWidget::DockWidgetClosable));
}

void TitleBarWidget::onCloseClicked()
{
	OBSDock *dock = getDock();
	dock->close();
}

void TitleBarWidget::onFloatClicked()
{
	OBSDock *dock = getDock();
	dock->toggleFloating();
}

void TitleBarWidget::onFeaturesChanged(QDockWidget::DockWidgetFeatures)
{
	updateButtons();
}

void TitleBarWidget::onTopLevelChanged(bool)
{
	updateButtons();
	floatButton->setAttribute(Qt::WA_UnderMouse, false);
#ifdef _WIN32
	OBSDock *dock = getDock();
	if (dock->isFloating() && dock->mouseState == OBSDock::MouseState::NotPressed)
		dock->setDropShadow(true);
#endif

	// Activate the window when [un]floating
	QWindow *window = this->window()->windowHandle();
	if (window)
		window->requestActivate();
}

OBSDock::OBSDock(const QString &title, QWidget *parent) : QDockWidget(title, parent)
{
	TitleBarWidget *titleBar = new TitleBarWidget(this);
	setTitleBarWidget(titleBar);

	installEventFilter(this);
	connect(this, &QDockWidget::visibilityChanged, this, &OBSDock::onVisibilityChanged);
}

void OBSDock::setVisible(bool visible)
{
	if (!settingFlags) {
		// Overrode this to remove the bypass flag that the base class sets
		// This way, all drags act the same, and transparency works
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

bool OBSDock::hasFeature(QDockWidget::DockWidgetFeature feature)
{
	return features() & feature;
}

bool OBSDock::isDraggable()
{
	return isFloating() || hasFeature(QDockWidget::DockWidgetMovable);
}

void OBSDock::toggleFloating()
{
	bool floating = !isFloating();
	setFloating(floating);

	if (!floating)
		raise();
}

bool OBSDock::eventFilter(QObject *o, QEvent *e)
{
	if (o != this)
		return false;

	switch (e->type()) {

	case QEvent::ChildAdded:
		return onChildAdded(static_cast<QChildEvent *>(e));

	case QEvent::ContextMenu:
		return onContextMenu(static_cast<QContextMenuEvent *>(e));

	case QEvent::HoverEnter:
		return onHoverEnter(static_cast<QHoverEvent *>(e));
	case QEvent::HoverMove:
		return onHoverMove(static_cast<QHoverEvent *>(e));
	case QEvent::HoverLeave:
		return onHoverLeave(static_cast<QHoverEvent *>(e));

	case QEvent::MouseButtonDblClick:
		return onMouseButtonDblClick(static_cast<QMouseEvent *>(e));
	case QEvent::MouseButtonPress:
		return onMouseButtonPress(static_cast<QMouseEvent *>(e));
	case QEvent::MouseMove:
		return onMouseMove(static_cast<QMouseEvent *>(e));
	case QEvent::MouseButtonRelease:
		return onMouseButtonRelease(static_cast<QMouseEvent *>(e));

	case QEvent::WindowActivate:
	case QEvent::WindowDeactivate:
		// Update the window border
		update();
		break;
	}

	return false;
}

bool OBSDock::onChildAdded(QChildEvent *event)
{
	QObject *child = event->child();

	if (child != this && !qobject_cast<QWidget *>(child)) {
		// Might be an event filter.
		// Keep installing ourselves as the first filter to be called
		// (this overrides the default resizer).
		QTimer::singleShot(1, this, [this]() { installEventFilter(this); });
	}

	return false;
}

bool OBSDock::onContextMenu(QContextMenuEvent *event)
{
	if (mouseState != MouseState::NotPressed)
		return true;

	if (OBSApp::IsWayland() && isFloating())
		// Global positioning doesn't work on Wayland
		return false;

	QPoint pos = event->pos();

	Qt::Edges edges = getResizeEdges(pos);
	if (edges)
		return true;

	QWidget *widget = childAt(pos);
	if (!qobject_cast<TitleBarWidget *>(widget))
		return false;

	clearCursor();
	QMenu *menuDocks = App()->GetMainWindow()->findChild<QMenu *>("menuDocks");
	menuDocks->exec(event->globalPos());
	return true;
}

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

	// Match the default implementation
	// A little extra space is given inside the title bar
	const QWidget *titleBar = titleBarWidget();
	bool inTitleBar = y < titleBar->y() + titleBar->height();
	int borderSize = inTitleBar ? 5 : 3;

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
		if (isDraggable()) {
			QWidget *widget = childAt(position);
			if (qobject_cast<TitleBarWidget *>(widget))
				return Qt::OpenHandCursor;
		}
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

bool OBSDock::onHoverEnter(QHoverEvent *event)
{
	return onHoverMove(event);
}

bool OBSDock::onHoverMove(QHoverEvent *event)
{
	if (mouseState != MouseState::NotPressed)
		return true;

	updateCursor(event->position().toPoint());
	return false;
}

bool OBSDock::onHoverLeave(QHoverEvent *)
{
	if (mouseState == MouseState::Resizing)
		onMouseButtonRelease(nullptr);

	clearCursor();
	return false;
}

bool OBSDock::onMouseButtonDblClick(QMouseEvent *event)
{
	if (mouseState != MouseState::NotPressed)
		return true;

	QPoint pos = event->pos();

	Qt::Edges edges = getResizeEdges(pos);
	if (edges)
		return true;

	if (!hasFeature(QDockWidget::DockWidgetFloatable))
		return false;

	QWidget *widget = childAt(pos);
	if (!qobject_cast<TitleBarWidget *>(widget))
		return false;

	if (event->button() != Qt::LeftButton)
		return true;

	// [Un]float on left double-click
	clearCursor();

	bool floating = !isFloating();
	setFloating(floating);
	if (!floating)
		raise();
	return true;
}

bool OBSDock::onMouseButtonPress(QMouseEvent *event)
{
	if (mouseState != MouseState::NotPressed)
		return true;

	if (event->button() != Qt::LeftButton)
		return false;

	pressPosition = event->pos();

#ifdef __SUPPORTS_SYSTEM_RESIZE
	pressEdges = getResizeEdges(pressPosition);
	if (pressEdges) {
		// Will do a system resize on drag
		mouseState = MouseState::Pressed;
		return true;
	}
#endif

	if (!isDraggable())
		return false;

	QWidget *widget = childAt(pressPosition);
	if (!qobject_cast<TitleBarWidget *>(widget))
		return false;

	bool floating = isFloating();
	bool floatable = hasFeature(QDockWidget::DockWidgetFloatable);

	if (floating && !floatable ||
	    (event->modifiers() & Qt::ControlModifier) && (floating || !OBSApp::IsWayland() && floatable)) {
		// Will do a system move on drag
		// Non-Wayland can use ctrl+drag to float into a system move
		mouseState = MouseState::CtrlPressed;
		updateCursor(pressPosition);
		return true;
	}

	// Will do a non-system move on drag
	mouseState = MouseState::Pressed;
	updateCursor(pressPosition);
	return false;
}

bool OBSDock::onMouseMove(QMouseEvent *event)
{
	if (mouseState == MouseState::Dragging)
		return false;

	if (mouseState == MouseState::CtrlPressed) {
		qreal dragDistance = (event->pos() - pressPosition).manhattanLength();
		if (dragDistance < QApplication::startDragDistance())
			return true;

		if (!isFloating()) {
			QRect bounds = geometry();

			// Float the dock widget
			setFloating(true);

			// Position the window properly
			// (it might still have a previous float location)
			bounds.moveTo(event->globalPosition().toPoint() - pressPosition);
			setGeometry(bounds);
		}

		setTranslucent(true);
#ifdef _WIN32
		setDropShadow(false);
#endif

		mouseState = MouseState::CtrlDragging;
		window()->windowHandle()->startSystemMove();
		return true;
	}

	if (mouseState != MouseState::Pressed)
		return true;

#ifdef __SUPPORTS_SYSTEM_RESIZE
	if (pressEdges) {
		qreal dragDistance = (event->pos() - pressPosition).manhattanLength();
		if (dragDistance < QApplication::startDragDistance())
			return true;

		setTranslucent(true);
#ifdef _WIN32
		setDropShadow(false);
#endif

		mouseState = MouseState::Resizing;
		window()->windowHandle()->startSystemResize(pressEdges);
		return true;
	}
#endif

	if (OBSApp::IsWayland()) {
		qreal dragDistance = (event->pos() - pressPosition).manhattanLength();
		if (dragDistance < QApplication::startDragDistance())
			return false;
	} else {
		QDockWidget::event(event);
		if (!mouseGrabber())
			// The grabber was not set yet
			return false;
	}

	mouseState = MouseState::Dragging;
	setTranslucent(true);

#ifdef _WIN32
	// Disable the drop shadow when moving the dock around
	// This prevents a lot of glitches (like when moving between screens)
	setDropShadow(false);
#endif

	return false;
}

bool OBSDock::onMouseButtonRelease(QMouseEvent *event)
{
	if (mouseState == MouseState::NotPressed)
		return false;

	if (event && event->button() != Qt::LeftButton)
		return true;

	bool wasDragging = mouseState == MouseState::Dragging;

	if (wasDragging) {
		setTranslucent(false);

#ifdef _WIN32
		// Re-enable the drop shadow
		setDropShadow(true);

		// The window may have been moved out of bounds, so fix that
		fixBounds();
#endif
	}
#ifndef _WIN32
	else if (mouseState == MouseState::CtrlDragging) {
		setTranslucent(false);

		// We need to fix the bounds later to get the proper screen and size
		QTimer::singleShot(50, this, [this]() { fixBounds(); });
	}
#endif

	mouseState = MouseState::NotPressed;
	if (event)
		updateCursor(event->pos());

	if (wasDragging) {
		// Disable animations on docking to make things look snappier
		QMainWindow *mainWindow = App()->GetMainWindow();
		mainWindow->setAnimated(false);

		QTimer::singleShot(1, this, [this, mainWindow]() {
			if (hasMouseTracking())
				releaseMouse();

			mainWindow->setAnimated(true);
		});
	}

	return false;
}

void OBSDock::paintEvent(QPaintEvent *)
{
	QStylePainter painter(this);

	if (isFloating()) {
		QStyleOptionFrame frameOpt;
		frameOpt.initFrom(this);
		painter.drawPrimitive(QStyle::PE_FrameDockWidget, frameOpt);
	}

	QStyleOptionDockWidget titleOpt;
	initStyleOption(&titleOpt);
	const QFont font = this->font();
	if (font == QApplication::font("QDockWidget")) {
		titleOpt.fontMetrics = QFontMetrics(font);
		painter.setFont(font);
	}
	painter.drawControl(QStyle::CE_DockWidgetTitle, titleOpt);
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
	if (!OBSBasic::Get()->Closing() && !warned) {
		QMetaObject::invokeMethod(App(), "Exec", Qt::QueuedConnection, Q_ARG(VoidFunc, msgBox));
	}

	QDockWidget::closeEvent(event);
}

#ifdef _WIN32
bool OBSDock::nativeEvent(const QByteArray &eventType, void *message, qintptr *result)
{
	if (!isFloating())
		return false;

	MSG *msg = reinterpret_cast<MSG *>(message);

	switch (msg->message) {

	case WM_SHOWWINDOW:
		if (msg->wParam) {
			HWND handle = (HWND)winId();
			// Don't allow rounded corners on Windows 11
			DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_DONOTROUND;
			DwmSetWindowAttribute(handle, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));
			// Don't draw a native border over ours
			COLORREF color = DWMWA_COLOR_NONE;
			DwmSetWindowAttribute(handle, DWMWA_BORDER_COLOR, &color, sizeof(color));
		}
		break;

	case WM_NCCALCSIZE:
		return true; // Causes the window to be drawn over the frame

	case WM_SIZING:
		// Notifies us that we're about to resize,
		// allowing us to prevent illegal resize operations
		{
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
		}
		break;

	case WM_EXITSIZEMOVE:
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

void OBSDock::initStyleOption(QStyleOptionDockWidget *option) const
{
	// Make this accessible to the other classes
	QDockWidget::initStyleOption(option);
}

#ifdef _WIN32
void OBSDock::setDropShadow(bool value)
{
	HWND hwnd = (HWND)winId();
	DWORD style = GetWindowLong(hwnd, GWL_STYLE);
	auto flags = WS_THICKFRAME | WS_CAPTION | WS_CLIPCHILDREN;

	if (value) {
		style |= flags;
		const MARGINS shadow = {1, 1, 1, 1};
		DwmExtendFrameIntoClientArea(hwnd, &shadow);
	} else {
		style &= ~flags;
	}

	SetWindowLong(hwnd, GWL_STYLE, style);
}
#endif

void OBSDock::setTranslucent(bool value)
{
	setWindowOpacity(value ? .67 : 1);
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

void OBSDock::onVisibilityChanged(bool visible)
{
	if (visible && !isFloating())
		// This fixes browser docks disappearing when tabbed
		raise();
}
