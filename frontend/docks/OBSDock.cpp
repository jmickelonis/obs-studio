#include "OBSDock.hpp"

#include <widgets/OBSBasic.hpp>

#include <QCheckBox>
#include <QMessageBox>
#include <QStylePainter>

#include "moc_OBSDock.cpp"

TitleBarWidget::TitleBarWidget(OBSDock *dock) : QWidget(dock)
{
	// Don't cover up the dock widget
	setAttribute(Qt::WA_NoSystemBackground);
	setAttribute(Qt::WA_TransparentForMouseEvents);

	// Get the dock's buttons (hopefully these will never be null!)
	floatButton = dock->findChild<QAbstractButton *>("qt_dockwidget_floatbutton");
	closeButton = dock->findChild<QAbstractButton *>("qt_dockwidget_closebutton");

	TitleBarLayout *layout = new TitleBarLayout(this);
	layout->setWidget(TitleBarLayout::FloatButton, floatButton);
	layout->setWidget(TitleBarLayout::CloseButton, closeButton);

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

void TitleBarWidget::onTopLevelChanged(bool)
{
	layout()->invalidate();

	// Stop from showing hover after setting floatable
	floatButton->setAttribute(Qt::WA_UnderMouse, false);

	// Activate the window when [un]floating
	QWindow *window = this->window()->windowHandle();
	if (window)
		window->requestActivate();
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

	QWidget *floatButton = items[FloatButton]->widget();
	QWidget *closeButton = items[CloseButton]->widget();

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
	if (!rect.isNull())
		items[CloseButton]->setGeometry(rect);
	rect = style->subElementRect(QStyle::SE_DockWidgetFloatButton, &opt, dock);
	if (!rect.isNull())
		items[FloatButton]->setGeometry(rect);
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
	mouseState = NotPressed;
	settingFlags = false;
	installEventFilter(this);

	TitleBarWidget *titleBar = new TitleBarWidget(this);
	setTitleBarWidget(titleBar);
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

bool OBSDock::eventFilter(QObject *o, QEvent *e)
{
	switch (e->type()) {

	case QEvent::MouseButtonPress: {
		if (mouseState != NotPressed)
			break;

		QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(e);
		if (mouseEvent->button() != Qt::LeftButton)
			break;

		mouseState = Pressed;
		break;
	}

	case QEvent::Move:
		if (mouseState == Pressed && mouseGrabber() && isFloating()) {
			mouseState = Dragging;
			setTranslucent(true);
		}
		break;

	case QEvent::MouseButtonRelease: {
		if (mouseState == NotPressed)
			break;

		QMouseEvent *mouseEvent = static_cast<QMouseEvent *>(e);
		if (mouseEvent->button() != Qt::LeftButton)
			break;

		if (mouseState == Dragging) {
			setTranslucent(false);

			/* Disable animations temporarily when docking, to make things look snappier.
				* This immediately snaps docks into place
				* (animations only occur while dragging/re-positioning).
				*/
			QMainWindow *mainWindow = App()->GetMainWindow();
			mainWindow->setAnimated(false);
			QTimer::singleShot(1, this, [mainWindow]() { mainWindow->setAnimated(true); });
		}

		mouseState = NotPressed;
		break;
	}

	default:
		break;
	}

	return QDockWidget::eventFilter(o, e);
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

void OBSDock::setTranslucent(bool value)
{
	setWindowOpacity(value ? .8 : 1);
}
