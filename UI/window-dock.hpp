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
	~TitleBarButton();

	QSize minimumSizeHint() const override;
	QSize sizeHint() const override;

protected:

    bool event(QEvent *event) override;
	void paintEvent(QPaintEvent *event) override;

private:

	mutable QSize *iconSize = nullptr;
	QAbstractButton *stockButton;

	QSize &getIconSize() const;

};


/* A custom layout that mimicks the default dock title bar.
 * Allows floating title bars to match the anchored ones.
 */
class TitleBarLayout : public QLayout
{

public:

	TitleBarLayout(QWidget *parent);
	~TitleBarLayout();

	enum Role { FloatButton, CloseButton, RoleCount };
	QWidget *getWidgetForRole(Role role) const;
	void setWidgetForRole(Role role, QWidget *widget);

	int count() const override;
	QLayoutItem *itemAt(int index) const override;
	QLayoutItem *takeAt(int index) override;
	void addItem(QLayoutItem *) override { }

	QSize sizeHint() const override;

protected:

	QSize maximumSize() const override;
    QSize minimumSize() const override;
	void setGeometry(const QRect &rect) override;

private:

	inline OBSDock *getDock() const
	{
		return qobject_cast<OBSDock*>(parentWidget()->parentWidget());
	}

	QVector<QLayoutItem*> items;

};


class TitleBarWidget : public QWidget
{
	Q_OBJECT

public:

	TitleBarWidget(OBSDock *parent = nullptr);
	~TitleBarWidget();

	inline OBSDock *getDock()
	{
		return qobject_cast<OBSDock*>(parentWidget());
	}

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


class OBSDock : public QDockWidget
{
	Q_OBJECT

public:

	OBSDock(QWidget *parent = nullptr);

	virtual void setVisible(bool visible) override;
	bool hasFeature(QDockWidget::DockWidgetFeature feature);
	bool isDraggable();
	void toggleFloating();

protected:

	virtual bool event(QEvent *event) override;
	virtual void paintEvent(QPaintEvent *event) override;
	virtual void closeEvent(QCloseEvent *event) override;
#ifdef _WIN32
	virtual bool nativeEvent(
		const QByteArray &eventType, void *message, long *result) override;
#endif

	void initStyleOption(QStyleOptionDockWidget *option) const override;
	friend class TitleBarLayout;
	friend class TitleBarWidget;

private:

	enum MouseState { NotPressed, Pressed, CtrlPressed, Dragging, CtrlDragging };
#ifdef _WIN32
	void setDropShadow(bool value);
#endif
	void setTranslucent(bool value);
	void fixBounds();

	Qt::Edges getResizeEdges(const QPoint *position);
	void updateCursor();
	void updateCursor(const QPoint *position);

	bool onMouseButtonDoubleClicked(QMouseEvent *event);
	bool onMouseButtonPressed(QMouseEvent *event);
	bool onMouseButtonReleased(QMouseEvent *event);
	bool onMouseMoved(QMouseEvent *event);
	bool onKeyPressed(QKeyEvent *event);

	QPoint initialCursorPosition;
	bool initialFloating;
	QScreen *initialScreen;
	MouseState mouseState = MouseState::NotPressed;
	QPoint pressPosition;
#if defined(_WIN32) || defined(__linux__)
	Qt::Edges pressEdges;
#endif
	bool settingFlags = false;

private slots:

	void onVisibilityChanged(bool visible);

};
