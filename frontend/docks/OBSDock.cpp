#include "OBSDock.hpp"

#include <widgets/OBSBasic.hpp>

#include <QCheckBox>
#include <QMessageBox>

#include "moc_OBSDock.cpp"

OBSDock::OBSDock(const QString &title, QWidget *parent) : QDockWidget(title, parent)
{
	installEventFilter(this);
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
	if (e->type() == QEvent::MouseButtonRelease && hasMouseTracking()) {
		/* Disable animations temporarily when docking, to make things look snappier.
		 * This immediately snaps docks into place
		 * (animations only occur while dragging/re-positioning).
		 */
		QMainWindow *mainWindow = App()->GetMainWindow();
		mainWindow->setAnimated(false);

		QTimer::singleShot(1, this, [this, mainWindow]() {
			releaseMouse();
			mainWindow->setAnimated(true);
		});
	}

	return QDockWidget::eventFilter(o, e);
}
