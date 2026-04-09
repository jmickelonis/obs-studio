#pragma once

#include <QDockWidget>
#include <QAbstractButton>
#include <QLayout>

class QCloseEvent;
class QShowEvent;
class QString;
class OBSDock;

/* Handles display and positioning of the title bar buttons,
 * so we can avoid using native title bars
 * (allowing the UI to be consistent across platforms).
 */
class TitleBarWidget : public QWidget {
	Q_OBJECT

public:
	TitleBarWidget(OBSDock *dock);

protected:
	virtual bool event(QEvent *event) override;

private:
	OBSDock *getDock() const;

	QAbstractButton *closeButton;
	QAbstractButton *floatButton;

private slots:
	void onFeaturesChanged(QDockWidget::DockWidgetFeatures features);
	void onTopLevelChanged(bool value);
};

class TitleBarLayout : public QLayout {

public:
	TitleBarLayout(QWidget *parent);
	~TitleBarLayout();

	enum Role { FloatButton, CloseButton };
	QWidget *getWidget(Role role) const;
	void setWidget(Role role, QWidget *widget);

	QLayoutItem *itemAt(int index) const override;
	QLayoutItem *takeAt(int index) override;
	void addItem(QLayoutItem *) override {}
	int count() const override;

	QSize sizeHint() const override;

protected:
	void setGeometry(const QRect &rect) override;
	QSize minimumSize() const override;

private:
	QVector<QLayoutItem *> items;

	OBSDock *getDock() const;
};

class OBSDock : public QDockWidget {
	Q_OBJECT

public:
	OBSDock(const QString &title, QWidget *parent = nullptr);
	inline OBSDock(QWidget *parent = nullptr) : OBSDock("", parent) {}

	bool hasFeature(QDockWidget::DockWidgetFeature feature);
	virtual void setVisible(bool visible) override;
	virtual void closeEvent(QCloseEvent *event);
	virtual void showEvent(QShowEvent *event);

protected:
	friend class TitleBarLayout;
	friend class TitleBarWidget;

	virtual bool eventFilter(QObject *o, QEvent *e) override;
	virtual void paintEvent(QPaintEvent *event) override;

private:
	enum MouseState { NotPressed, Pressed, Dragging };
	MouseState mouseState;
	bool settingFlags;

	void setTranslucent(bool value);
};
