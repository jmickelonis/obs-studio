#include "window-dock.hpp"
#include "obs-app.hpp"

#include <QMessageBox>
#include <QCheckBox>

#include <QMenu>
#include <QScreen>
#include <QStyle>
#include <QStyleOptionFrame>
#include <QStylePainter>
#include <QWindow>
#include <iostream>

#ifdef _WIN32
#include <windowsx.h>
#include <dwmapi.h>
#pragma comment (lib,"Dwmapi.lib")
#endif


static inline bool hasFeature(
	const QDockWidget *dock, QDockWidget::DockWidgetFeature feature)
{
	return (dock->features() & feature) == feature;
}


TitleBarButton::TitleBarButton(QAbstractButton *stockButton)
		: stockButton(stockButton)
{
	setFocusPolicy(Qt::NoFocus);
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
	const QIcon qIcon = icon();

	if (!qIcon.isNull()) {
		const QSize qSize = qIcon.actualSize(getIconSize());
		size += qMax(qSize.width(), qSize.height());
	}

	return QSize(size, size);
}

void TitleBarButton::enterEvent(QEvent *event)
{
    if (isEnabled())
		update();
    QAbstractButton::enterEvent(event);
}

void TitleBarButton::leaveEvent(QEvent *event)
{
    if (isEnabled())
		update();
    QAbstractButton::leaveEvent(event);
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
        style->drawPrimitive(QStyle::PE_PanelButtonTool, &opt, &painter, stockButton);
    }

    opt.icon = icon();
    opt.features = QStyleOptionToolButton::None;
    opt.arrowType = Qt::NoArrow;
    opt.iconSize = getIconSize();
    style->drawComplexControl(QStyle::CC_ToolButton, &opt, &painter, stockButton);
}

bool TitleBarButton::event(QEvent *event)
{
    switch (event->type()) {
    case QEvent::StyleChange:
    case QEvent::ScreenChangeInternal:
        iconSize = -1;
        break;
    default:
        break;
    }
    return QAbstractButton::event(event);
}

QSize TitleBarButton::getIconSize() const
{
    if (iconSize < 0)
        iconSize = stockButton->style()->pixelMetric(
			QStyle::PM_SmallIconSize, nullptr, stockButton);
    return QSize(iconSize, iconSize);
}


TitleBarLayout::TitleBarLayout(QWidget *parent)
	: QLayout(parent), items(RoleCount, 0)
{
}

TitleBarLayout::~TitleBarLayout()
{
	qDeleteAll(items);
}

void TitleBarLayout::setVertical(bool value)
{
    if (value == vertical)
        return;
    vertical = value;
    invalidate();
    parentWidget()->update();
}

QLayoutItem *TitleBarLayout::getItemForRole(Role role) const
{
    return items.at(role);
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
    for (int i = 0; i < items.count(); ++i)
        if (items.at(i))
            ++result;
    return result;
}

QLayoutItem *TitleBarLayout::itemAt(int index) const
{
    int count = 0;
    for (int i = 0; i < items.count(); ++i) {
        QLayoutItem *item = items.at(i);
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
    for (int i = 0; i < items.count(); ++i) {
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
	OBSDock *dock = qobject_cast<OBSDock*>(parentWidget()->parentWidget());
	QStyle *style = dock->style();

	QSize closeSize = hasFeature(dock, QDockWidget::DockWidgetClosable)
		? items[CloseButton]->widget()->sizeHint()
		: QSize(0, 0);

	QSize floatSize = hasFeature(dock, QDockWidget::DockWidgetFloatable)
		? items[FloatButton]->widget()->sizeHint()
		: QSize(0, 0);

	int buttonHeight =
		vertical ? qMax(closeSize.width(), floatSize.width())
		: qMax(closeSize.height(), floatSize.height());
	int margin = style->pixelMetric(
		QStyle::PM_DockWidgetTitleMargin, nullptr, dock);
	
	int h = qMax(buttonHeight + 2, dock->fontMetrics().height() + 2 * margin);
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
	OBSDock *dock = qobject_cast<OBSDock*>(parentWidget()->parentWidget());
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

OBSDock *TitleBarWidget::getDock()
{
	return qobject_cast<OBSDock*>(parentWidget());
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
		if (QMenu *menuDocks = App()->GetMainWindow()->findChild<QMenu*>("menuDocks"))
			menuDocks->exec(QCursor::pos());
			return true;
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
		hasFeature(dock, QDockWidget::DockWidgetFloatable));

	closeButton->setIcon(style->standardIcon(
		QStyle::SP_TitleBarCloseButton, &opt, dock));
	closeButton->setVisible(
		hasFeature(dock, QDockWidget::DockWidgetClosable));
}

void TitleBarWidget::onCloseClicked()
{
	OBSDock *dock = getDock();
	dock->close();
}

void TitleBarWidget::onFloatClicked()
{
	OBSDock *dock = getDock();
	dock->setFloating(!dock->isFloating());
}

void TitleBarWidget::onFeaturesChanged(QDockWidget::DockWidgetFeatures)
{
	updateButtons();
}

void TitleBarWidget::onTopLevelChanged(bool)
{
	updateButtons();

	// if (floating)
	// 	return;

	// QMainWindow *mainWindow = App()->GetMainWindow();

	// if (!mainWindow)
	// 	return;

	// OBSDock *dock = getDock();
	// QList<QDockWidget *> siblings = mainWindow->tabifiedDockWidgets(dock);

	// if (siblings.count())
	// 	mainWindow->tabifyDockWidget(siblings.first(), dock);
}


OBSDock::OBSDock(QWidget *parent)
	: QDockWidget(parent)
{
	TitleBarWidget *titleBar = new TitleBarWidget(this);
	setTitleBarWidget(titleBar);
}

bool OBSDock::event(QEvent *event)
{
	switch (event->type()) {
	case QEvent::WindowActivate:
	case QEvent::WindowDeactivate:
		update();
		break;
	}

	if (!isFloating())
		goto END;

	switch (event->type()) {

#ifdef _WIN32
	case QEvent::MouseMove:
		// Disable the drop shadow when moving the dock around
		// This prevents a lot of glitches (like when moving between screens)
		setDropShadow(false);
		break;
	case QEvent::WindowActivate:
		// Show the drop shadow when the window has been activated
		setDropShadow(true);
		break;
#endif

	case QEvent::MouseButtonRelease:
		// The window may have been moved out of bounds, so fix that
		fixBounds();
#ifdef _WIN32
		// Re-enable the drop shadow
		setDropShadow(true);
#endif
		break;

	}

	END:
	return QDockWidget::event(event);
}

void OBSDock::paintEvent(QPaintEvent *)
{
	QStylePainter p(this);

	if (isFloating()) {
		QStyleOptionFrame frameOpt;
		frameOpt.init(this);
		p.drawPrimitive(QStyle::PE_FrameDockWidget, frameOpt);
	}

	QStyleOptionDockWidget titleOpt;
	initStyleOption(&titleOpt);
	titleOpt.fontMetrics = QFontMetrics(font());
	p.setFont(font());
	p.drawControl(QStyle::CE_DockWidgetTitle, titleOpt);
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

#ifdef _WIN32
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

#ifdef _WIN32
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
		 style &= ~(WS_THICKFRAME | WS_CAPTION | WS_CLIPCHILDREN);
	}

	SetWindowLong(hwnd, GWL_STYLE, style);
}
#endif
