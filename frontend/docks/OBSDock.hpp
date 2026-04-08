#pragma once

#include <QDockWidget>

class QCloseEvent;
class QShowEvent;
class QString;

class OBSDock : public QDockWidget {
	Q_OBJECT

public:
	OBSDock(const QString &title, QWidget *parent = nullptr);
	inline OBSDock(QWidget *parent = nullptr) : OBSDock("", parent) {}

	virtual void setVisible(bool visible) override;
	virtual void closeEvent(QCloseEvent *event);
	virtual void showEvent(QShowEvent *event);

protected:
	virtual bool eventFilter(QObject *o, QEvent *e) override;

private:
	void setTranslucent(bool value);

	enum MouseState { NotPressed, Pressed, Dragging };
	MouseState mouseState;
	bool settingFlags;
};
