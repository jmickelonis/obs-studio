#include "window-dock.hpp"
#include "obs-app.hpp"

#include <QMessageBox>
#include <QCheckBox>

#include <QMenu>
#include <QScreen>
#include <QStyle>
#include <QStyleOptionFrame>
#include <QStylePainter>
#include <QTimer>
#include <QWindow>

#ifdef __WINDOWS__
#include <windowsx.h>
#include <dwmapi.h>
#pragma comment (lib,"Dwmapi.lib")
#endif

#if defined(__WINDOWS__) || defined(__linux__)
// Mac doesn't support this yet
#define __SUPPORTS_SYSTEM_RESIZE
#endif


TitleBarButton::TitleBarButton(QAbstractButton *stockButton)
	: stockButton(stockButton)
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

	int size = 2 * stockButton->style()->pixelMetric(
		QStyle::PM_DockWidgetTitleBarButtonMargin, nullptr, stockButton);

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
    opt.init(this);
    opt.state |= QStyle::State_AutoRaise;

    if (style->styleHint(
			QStyle::SH_DockWidget_ButtonsHaveFrame, nullptr, stockButton)) {
        if (isEnabled() && underMouse() && !isChecked() && !isDown())
            opt.state |= QStyle::State_Raised;
        if (isChecked())
            opt.state |= QStyle::State_On;
        if (isDown())
            opt.state |= QStyle::State_Sunken;
        style->drawPrimitive(
			QStyle::PE_PanelButtonTool, &opt, &painter, stockButton);
    }

    opt.icon = icon();
    opt.features = QStyleOptionToolButton::None;
    opt.arrowType = Qt::NoArrow;
    opt.iconSize = getIconSize();
    style->drawComplexControl(
		QStyle::CC_ToolButton, &opt, &painter, stockButton);
}


QSize &TitleBarButton::getIconSize() const
{
    if (!iconSize) {
        int n = stockButton->style()->pixelMetric(
			QStyle::PM_SmallIconSize, nullptr, stockButton);
		iconSize = new QSize(n, n);
	}
	return *iconSize;
}


TitleBarLayout::TitleBarLayout(QWidget *parent)
	: QLayout(parent), items(RoleCount, nullptr)
{
}

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
    }
	else {
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

	QSize closeSize = dock->hasFeature(QDockWidget::DockWidgetClosable)
		? items[CloseButton]->widget()->sizeHint()
		: QSize(0, 0);

	QSize floatSize = dock->hasFeature(QDockWidget::DockWidgetFloatable)
		? items[FloatButton]->widget()->sizeHint()
		: QSize(0, 0);

	bool vertical = dock->hasFeature(QDockWidget::DockWidgetVerticalTitleBar);

	int buttonHeight = vertical
		? qMax(closeSize.width(), floatSize.width())
		: qMax(closeSize.height(), floatSize.height());
	int margin = style->pixelMetric(
		QStyle::PM_DockWidgetTitleMargin, nullptr, dock);
	
	int h = qMax(buttonHeight, dock->fontMetrics().height()) + 2 * margin;
	int w = (vertical ? closeSize.height() : closeSize.width())
			+ (vertical ? floatSize.height() : floatSize.width())
			+ h + 3 * margin;
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
	QRect rect = style->subElementRect(
		QStyle::SE_DockWidgetCloseButton, &opt, dock);
	if (!rect.isNull())
		item->setGeometry(rect);

	item = items[FloatButton];
	rect = style->subElementRect(
		QStyle::SE_DockWidgetFloatButton, &opt, dock);
	if (!rect.isNull())
		item->setGeometry(rect);
}


TitleBarWidget::TitleBarWidget(OBSDock *dock)
	: QWidget(dock)
{
	// Don't cover up the dock widget
	setAttribute(Qt::WA_NoSystemBackground);

	// Hopefully these will never be null!
	closeButton = new TitleBarButton(
		dock->findChild<QAbstractButton*>("qt_dockwidget_closebutton"));
	floatButton = new TitleBarButton(
		dock->findChild<QAbstractButton*>("qt_dockwidget_floatbutton"));

	TitleBarLayout *layout = new TitleBarLayout(this);
	layout->setSizeConstraint(QLayout::SetMinAndMaxSize);
	layout->setWidgetForRole(TitleBarLayout::FloatButton, floatButton);
	layout->setWidgetForRole(TitleBarLayout::CloseButton, closeButton);

	updateButtons();

	connect(closeButton, &QAbstractButton::clicked,
		this, &TitleBarWidget::onCloseClicked);
	connect(floatButton, &QAbstractButton::clicked,
		this, &TitleBarWidget::onFloatClicked);
	connect(dock, &QDockWidget::featuresChanged,
		this, &TitleBarWidget::onFeaturesChanged);
	connect(dock, &QDockWidget::topLevelChanged,
		this, &TitleBarWidget::onTopLevelChanged);
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

	case QEvent::MouseButtonPress:
	case QEvent::MouseButtonRelease:
	case QEvent::MouseButtonDblClick:
	case QEvent::MouseMove:
		// Let OBSDock handle mouse events
		event->ignore();
		return false;

	case QEvent::ContextMenu:
		// Our custom title bar covers up the dock widget, which breaks context menus
		// We'll trigger the docks menu directly
		if (QMenu *menuDocks = App()->GetMainWindow()->findChild<QMenu*>("menuDocks")) {
			menuDocks->exec(QCursor::pos());
			return true;
		}
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

	floatButton->setIcon(style->standardIcon(
		QStyle::SP_TitleBarNormalButton, &opt, dock));
	floatButton->setVisible(
		dock->hasFeature(QDockWidget::DockWidgetFloatable));

	closeButton->setIcon(style->standardIcon(
		QStyle::SP_TitleBarCloseButton, &opt, dock));
	closeButton->setVisible(
		dock->hasFeature(QDockWidget::DockWidgetClosable));
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
	OBSDock *dock = getDock();
	dock->updateCursor();
}

void TitleBarWidget::onTopLevelChanged(bool)
{
	updateButtons();
}


OBSDock::OBSDock(QWidget *parent)
	: QDockWidget(parent)
{
	TitleBarWidget *titleBar = new TitleBarWidget(this);
	setTitleBarWidget(titleBar);

#ifdef __linux__
	// Set the type to Dialog to always keep the windows showing
	setAttribute(Qt::WA_X11NetWmWindowTypeDialog);
#endif
}


#ifdef __linux__
void OBSDock::setVisible(bool visible)
{
	if (!settingFlags) {
		// Overrode this to remove the bypass flag that the base class sets
		// This way, all drags act the same, and transparency works
		// We do need to set it back when plugging (un-floating) docks
		// (without it, the docks can pop back out after dragging them back in)
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
#endif

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
#ifdef __linux__
	if (!floating) {
		Qt::WindowFlags flags = windowFlags();
		if (!(flags & Qt::BypassWindowManagerHint)) {
			// Stop the dock from popping back out
			settingFlags = true;
			flags |= Qt::BypassWindowManagerHint;
			bool wasVisible = isVisible();
			setWindowFlags(flags);
			if (wasVisible)
				setVisible(true);
			settingFlags = false;
		}
	}
#endif
	setFloating(floating);
}


static void enableAnimationsLater()
{
	static QTimer *timer;

	if (!timer) {
		timer = new QTimer();
		QMainWindow *mainWindow = App()->GetMainWindow();
		QObject::connect(timer, &QTimer::timeout,
			[mainWindow]() {
				mainWindow->setAnimated(true);
			});
	}

	timer->stop();
	timer->start(1);
}

bool OBSDock::event(QEvent *event)
{
	switch (event->type()) {

#ifdef __SUPPORTS_SYSTEM_RESIZE
	case QEvent::ChildAdded:
		{
			QChildEvent *childEvent = static_cast<QChildEvent*>(event);
			QObject *child = childEvent->child();

			if (qobject_cast<QWidget*>(child))
				break;  // We only care about objects, not widgets

			// Since we handle system resize, we need to disable the default resizer
			// Unfortunately this isn't easily done,
			// so we have to remove any event filters that may have been installed
			QTimer::singleShot(1, this, [child, this]() {
				removeEventFilter(child);
			});
		}
		break;
#endif

	// Update the cursor as the mouse is moved over us
	case QEvent::HoverEnter:
	case QEvent::HoverMove:
		{
			QHoverEvent *hoverEvent = static_cast<QHoverEvent*>(event);
			QPoint point = hoverEvent->pos();
			updateCursor(&point);
		}
		break;
	case QEvent::HoverLeave:
		updateCursor(nullptr);
		break;

	case QEvent::WindowActivate:
#ifdef __WINDOWS__
		// Show the drop shadow when the window has been activated
		if (isFloating() && mouseState == MouseState::NotPressed)
			setDropShadow(true);
		// fall-through
#endif
	case QEvent::WindowDeactivate:
		update();
		break;
	
	case QEvent::Move:
		if (mouseState == MouseState::CtrlDragging) {
			// This can be called after doing a system drag
			// (the events aren't received until the entire operation is over)

			// We need to fix the bounds later to get the proper screen
			QTimer::singleShot(1, this, [this]() {
				fixBounds();
			});
		}
		break;

	case QEvent::MouseButtonPress:
		if (!onMouseButtonPressed(static_cast<QMouseEvent*>(event)))
			return false;
		break;
	
	case QEvent::MouseButtonDblClick:
		if (!onMouseButtonDoubleClicked(static_cast<QMouseEvent*>(event)))
			return false;
		break;

	case QEvent::MouseMove:
		if (!onMouseMoved(static_cast<QMouseEvent*>(event)))
			return false;
		break;

	case QEvent::MouseButtonRelease:
		if (!onMouseButtonReleased(static_cast<QMouseEvent*>(event)))
			return false;
		break;
	
	case QEvent::KeyPress:
		if (!onKeyPressed(static_cast<QKeyEvent*>(event)))
			return false;
		break;

	default:
		break;

	}

	return QDockWidget::event(event);
}

void OBSDock::paintEvent(QPaintEvent *)
{
	QStylePainter painter(this);

	if (isFloating()) {
		QStyleOptionFrame frameOpt;
		frameOpt.init(this);
		painter.drawPrimitive(QStyle::PE_FrameDockWidget, frameOpt);
	}

	QStyleOptionDockWidget titleOpt;
	initStyleOption(&titleOpt);
	const QFont font = this->font();
	titleOpt.fontMetrics = QFontMetrics(font);
	painter.setFont(font);
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
			config_set_bool(App()->GlobalConfig(), "General",
					"WarnedAboutClosingDocks", true);
			config_save_safe(App()->GlobalConfig(), "tmp", nullptr);
		}
	};

	bool warned = config_get_bool(App()->GlobalConfig(), "General",
				      "WarnedAboutClosingDocks");
	if (!warned) {
		QMetaObject::invokeMethod(App(), "Exec", Qt::QueuedConnection,
					  Q_ARG(VoidFunc, msgBox));
	}

	QDockWidget::closeEvent(event);
}

#ifdef __WINDOWS__
bool OBSDock::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
	if (!isFloating())
		goto END;

	MSG* msg = reinterpret_cast<MSG*>(message);

	switch (msg->message) {

	case WM_NCCALCSIZE:
		return true;  // Causes the window to be drawn over the frame

	case WM_NCHITTEST:
		// Determines which part of the window corresponds to the cursor location
		{
			RECT rect;
			GetWindowRect(HWND(winId()), &rect);

			long x = GET_X_LPARAM(msg->lParam);
			long y = GET_Y_LPARAM(msg->lParam);

			if (x < rect.left || x >= rect.right
					|| y < rect.top || y >= rect.bottom)
				// We're not over the window
				break;

			const LONG borderThickness = (LONG)(5 * devicePixelRatioF());

			if (x < rect.left + borderThickness)
				*result = HTLEFT;
			else if (x >= rect.right - borderThickness)
				*result = HTRIGHT;

			if (y < rect.top + borderThickness)
				*result = *result == HTLEFT ? HTTOPLEFT
					: *result == HTRIGHT ? HTTOPRIGHT
					: HTTOP;
			else if (y >= rect.bottom - borderThickness)
				*result = *result == HTLEFT ? HTBOTTOMLEFT
					: *result == HTRIGHT ? HTBOTTOMRIGHT
					: HTBOTTOM;

			if (!*result)
				// We aren't over a border area so it must be the client
				*result = HTCLIENT;
			return true;
		}
		break;

	case WM_SIZING:
		// Notifies us that we're about to resize,
		// allowing us to prevent illegal resize operations
		{
			WPARAM edge = msg->wParam;
			RECT& rect = *reinterpret_cast<RECT*>(msg->lParam);

			HMONITOR monitor = MonitorFromWindow(
				HWND(winId()), MONITOR_DEFAULTTONEAREST);
			MONITORINFO monitorInfo;
			monitorInfo.cbSize = sizeof(MONITORINFO);
			GetMonitorInfo(monitor, &monitorInfo);
			RECT& screenBounds = monitorInfo.rcMonitor;

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
				}
				else {
					// Keep the title bar completely on screen
					TitleBarWidget* titleBar = findChild<TitleBarWidget*>();
					int yMax = screenBounds.bottom
						- (int)((titleBar->y() + titleBar->height()) * devicePixelRatioF());
					if (rect.top > yMax)
						rect.top = yMax;
				}
			}
		}
		break;

	default:
		break;
	}

	END:
	return QDockWidget::nativeEvent(eventType, message, result);
}
#endif


void OBSDock::initStyleOption(QStyleOptionDockWidget *option) const
{
	// Make this accessible to the other classes
	QDockWidget::initStyleOption(option);
}


#ifdef __WINDOWS__
void OBSDock::setDropShadow(bool value)
{
	HWND hwnd = (HWND)winId();
	DWORD style = GetWindowLong(hwnd, GWL_STYLE);
	auto flags = WS_THICKFRAME | WS_CAPTION | WS_CLIPCHILDREN;

	if (value) {
		style |= flags;
		const MARGINS shadow = { 1, 1, 1, 1 };
		DwmExtendFrameIntoClientArea(hwnd, &shadow);
	}
	else {
		style &= ~flags;
	}

	SetWindowLong(hwnd, GWL_STYLE, style);
}
#endif

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
	}
	else {
		int xMax = screenBounds.right() - this->width() + 1;
		if (x > xMax)
			x = xMax;
	}

	if (y < yMin) {
		y = yMin;
	}
	else {
		TitleBarWidget* titleBar = findChild<TitleBarWidget*>();
		int yMax = screenBounds.bottom() - (titleBar->y() + titleBar->height()) + 1;
		if (y > yMax)
			y = yMax;
	}

	move(x, y);
}


Qt::CursorShape OBSDock::getCursorShape(const QPoint *position)
{
	if (!position)
		return Qt::ArrowCursor;

	switch (mouseState) {
	case MouseState::Pressed:
	case MouseState::CtrlPressed:
	case MouseState::Dragging:
		return Qt::ClosedHandCursor;
	default:
		break;
	}

	if (isFloating()) {
		Qt::Edges edges = getResizeEdges(position);

		if (edges & Qt::LeftEdge)
			return edges & Qt::TopEdge ? Qt::SizeFDiagCursor
				: edges & Qt::BottomEdge ? Qt::SizeBDiagCursor
				: Qt::SizeHorCursor;
		else if (edges & Qt::RightEdge)
			return edges & Qt::TopEdge ? Qt::SizeBDiagCursor
				: edges & Qt::BottomEdge ? Qt::SizeFDiagCursor
				: Qt::SizeHorCursor;
		else if (edges & (Qt::TopEdge | Qt::BottomEdge))
			return Qt::SizeVerCursor;
	}

	if (isDraggable()) {
		QWidget *widget = childAt(*position);
		if (qobject_cast<TitleBarWidget*>(widget))
			return Qt::OpenHandCursor;
	}

	return Qt::ArrowCursor;
}

Qt::Edges OBSDock::getResizeEdges(const QPoint *position)
{
	Qt::Edges edges;

	if (!position || !isFloating())
		return edges;

	const int x = position->x();
	const int y = position->y();

#ifdef __SUPPORTS_SYSTEM_RESIZE
	const int borderSize = 4;
#else
	const int borderSize = 1;
#endif

	if (x < borderSize)
		edges |= Qt::LeftEdge;
	else if (x >= width() - borderSize)
		edges |= Qt::RightEdge;

	if (y < borderSize)
		edges |= Qt::TopEdge;
	else if (y >= height() - borderSize)
		edges |= Qt::BottomEdge;

	return edges;
}

void OBSDock::updateCursor()
{
	QPoint position = QCursor::pos(screen());
	updateCursor(geometry().contains(position) ? &position : nullptr);
}

void OBSDock::updateCursor(const QPoint *position)
{
	Qt::CursorShape shape = getCursorShape(position);
	if (shape != Qt::ArrowCursor)
		setCursor(shape);
	else
		unsetCursor();
}


bool OBSDock::onMouseButtonDoubleClicked(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return true;

	// Prevent dragging after double click and hold
	mouseState = MouseState::NotPressed;

	if (hasFeature(QDockWidget::DockWidgetMovable))
		toggleFloating();

	return false;
}

bool OBSDock::onMouseButtonPressed(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return true;

	pressPosition = event->pos();
	QTimer::singleShot(1, this, [this]() {
		updateCursor(&pressPosition);
	});

#ifdef __SUPPORTS_SYSTEM_RESIZE
	pressEdges = getResizeEdges(&pressPosition);
	if (pressEdges) {
		// Pressed on the resizable border
		mouseState = MouseState::Pressed;
		// Wait for the drag operation
		return false;
	}
#endif

	if (!isDraggable())
		return false;

	QWidget *widget = childAt(pressPosition);
	if (!qobject_cast<TitleBarWidget*>(widget))
		// Didn't press on the title bar
		return false;

	initialScreen = screen();
	initialCursorPosition = QCursor::pos(initialScreen);
	initialFloating = isFloating();

	if (event->modifiers() & Qt::ControlModifier) {
		mouseState = MouseState::CtrlPressed;
		// Don't let the base class do anything with Ctrl+drag
		return false;
	}
	
	mouseState = MouseState::Pressed;
	return true;
}

bool OBSDock::onMouseButtonReleased(QMouseEvent *event)
{
	if (event->button() != Qt::LeftButton)
		return true;

	if (mouseState == MouseState::Dragging) {
		setWindowOpacity(1);

#ifdef __WINDOWS__
		// Re-enable the drop shadow
		setDropShadow(true);

		// The window may have been moved out of bounds, so fix that
		fixBounds();
#endif

		if (hasFeature(QDockWidget::DockWidgetMovable)) {
			// Disabling animations then re-enabling after we're docked
			// improves the UI experience
			App()->GetMainWindow()->setAnimated(false);
			enableAnimationsLater();

#ifdef __linux__
			Qt::WindowFlags flags = windowFlags();
			if (!(flags & Qt::BypassWindowManagerHint)) {
				// Stop the dock from popping back out
				settingFlags = true;
				flags |= Qt::BypassWindowManagerHint;
				setWindowFlags(flags);
				QTimer::singleShot(1, this, [this]() {
					settingFlags = false;
				});
			}
#endif
		}
	}

	mouseState = MouseState::NotPressed;

	QPoint position = event->pos();
	updateCursor(&position);

	return true;
}

bool OBSDock::onMouseMoved(QMouseEvent *event)
{
	if (mouseState == MouseState::CtrlPressed) {
		qreal dragDistance = (event->pos() - pressPosition).manhattanLength();
		if (dragDistance < QApplication::startDragDistance())
			return false;

		// We Ctrl-dragged far enough

		if (!isFloating()) {
			QRect bounds = geometry();

			// Float the dock widget
			setFloating(true);
			
			// Position the window properly
			// (it might still have a previous float location)
			bounds.moveTo(event->globalPos() - pressPosition);
			setGeometry(bounds);
		}

		// Let the WM handle the rest
		window()->windowHandle()->startSystemMove();
		mouseState = MouseState::CtrlDragging;
		return false;
	}

	if (mouseState == MouseState::Pressed) {
#ifdef __SUPPORTS_SYSTEM_RESIZE
		if (pressEdges) {
			window()->windowHandle()->startSystemResize(pressEdges);
			mouseState = MouseState::NotPressed;
			return false;
		}
#endif

		QDockWidget::event(event);
		if (mouseGrabber()) {
			// The grabber was set which means we must be dragging
			mouseState = MouseState::Dragging;
			setWindowOpacity(.75);

#ifdef __WINDOWS__
			// Disable the drop shadow when moving the dock around
			// This prevents a lot of glitches (like when moving between screens)
			setDropShadow(false);
#endif
		}
		return false;
	}

	return true;
}

bool OBSDock::onKeyPressed(QKeyEvent *event)
{
	if (event->key() != Qt::Key_Escape)
		return true;

	// Try to cancel drag operations when hitting Esc

	if (mouseState == MouseState::Dragging) {
		// Just force the mouse cursor back to its original position
		// Faking move events and such doesn't seem to work
		QCursor::setPos(initialScreen, initialCursorPosition);

		setWindowOpacity(1);

		QTimer::singleShot(1, this, [this]() {
			// This method aborts the drag operation in the base class
			setFloating(initialFloating);

#ifdef __WINDOWS__
			if (initialFloating)
				// Re-enable the drop shadow
				setDropShadow(true);
#endif

			mouseState = MouseState::NotPressed;
			updateCursor();
		});
	}
	else if (mouseState == MouseState::CtrlDragging) {
		// Fortunately, this is a lot cleaner than the default drags
		mouseState = MouseState::NotPressed;
		if (!initialFloating)
			toggleFloating();
	}

	return false;
}
