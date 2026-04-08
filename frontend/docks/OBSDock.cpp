#include "OBSDock.hpp"

#include <widgets/OBSBasic.hpp>

#include <QCheckBox>
#include <QMessageBox>

#include "moc_OBSDock.cpp"

OBSDock::OBSDock(const QString &title, QWidget *parent) : QDockWidget(title, parent)
{
	mouseState = NotPressed;
	settingFlags = false;
	installEventFilter(this);
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

void OBSDock::setTranslucent(bool value)
{
	setWindowOpacity(value ? .8 : 1);
}
