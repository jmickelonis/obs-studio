#pragma once

#include <QDockWidget>

#include <QAbstractButton>
#include <QLayout>
#include <QMouseEvent>
#include <QStyleOptionDockWidget>


class OBSDock;


/* A custom title bar button that uses the stock dock button to style itself
 * (so we can continue to theme everything the same way).
 */
class TitleBarButton : public QAbstractButton
{

public:

	TitleBarButton(QAbstractButton *stockButton);

	QSize minimumSizeHint() const override;
	QSize sizeHint() const override;
	void enterEvent(QEvent *event) override;
	void leaveEvent(QEvent *event) override;
	void paintEvent(QPaintEvent *event) override;

protected:

    bool event(QEvent *event) override;

private:

	mutable int iconSize = -1;
	QAbstractButton *stockButton;

	QSize getIconSize() const;

};


/* A custom layout that mimicks the default dock title bar.
 * Allows floating title bars to match the anchored ones.
 */
class TitleBarLayout : public QLayout
{

public:

	TitleBarLayout(QWidget *parent);
	~TitleBarLayout();

	void setVertical(bool value);

	enum Role { FloatButton, CloseButton, RoleCount };
	QLayoutItem *getItemForRole(Role role) const;
	QWidget *getWidgetForRole(Role role) const;
	void setWidgetForRole(Role role, QWidget *widget);

	int count() const override;
	QLayoutItem *itemAt(int index) const override;
	QLayoutItem *takeAt(int index) override;
	void addItem(QLayoutItem *) override { }

	QSize sizeHint() const override;
	QSize maximumSize() const override;
    QSize minimumSize() const override;

	void setGeometry(const QRect &rect) override;

private:

	QVector<QLayoutItem*> items;
	bool vertical = false;

};


class TitleBarWidget : public QWidget
{
	Q_OBJECT

public:

	TitleBarWidget(OBSDock *parent = nullptr);
	~TitleBarWidget();

	OBSDock *getDock();

protected:

	virtual bool event(QEvent *event) override;

private:

	TitleBarButton *closeButton;
	TitleBarButton *floatButton;

	void updateButtons();

private slots:

	void onCloseClicked();
	void onFloatClicked();
	void onFeaturesChanged(QDockWidget::DockWidgetFeatures features);
	void onTopLevelChanged(bool value);

};


class OBSDock : public QDockWidget {
	Q_OBJECT

public:
	OBSDock(QWidget *parent = nullptr);
	virtual bool event(QEvent *event) override;
	virtual void paintEvent(QPaintEvent *event) override;
	virtual void closeEvent(QCloseEvent *event);

protected:
#ifdef _WIN32
	virtual bool nativeEvent(const QByteArray &eventType, void *message, long *result);
#endif
	void initStyleOption(QStyleOptionDockWidget *option) const;
	friend class TitleBarLayout;
	friend class TitleBarWidget;

private:
	void fixBounds();
#ifdef _WIN32
	void setDropShadow(bool value);
#endif

};
