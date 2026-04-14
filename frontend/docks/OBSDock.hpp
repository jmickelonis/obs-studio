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

#if defined(_WIN32) || defined(__linux__)
#define __QT_SUPPORTS_SYSTEM_RESIZE
#endif

class OBSDock : public QDockWidget {
	Q_OBJECT

public:
	OBSDock(const QString &title, QWidget *parent = nullptr);
	inline OBSDock(QWidget *parent = nullptr) : OBSDock("", parent) {}

	Q_PROPERTY(bool dropShadow READ isDropShadow WRITE setDropShadow)
	bool isDropShadow();
	void setDropShadow(bool value);

	bool hasFeature(QDockWidget::DockWidgetFeature feature);
	virtual void setVisible(bool visible) override;
	virtual void closeEvent(QCloseEvent *event);
	virtual void showEvent(QShowEvent *event);

protected:
	friend class TitleBarLayout;
	friend class TitleBarWidget;

	QAbstractButton *closeButton;
	QAbstractButton *floatButton;

	virtual bool event(QEvent *event) override;
	virtual void paintEvent(QPaintEvent *event) override;
#ifdef _WIN32
	virtual bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#endif

private:
	enum MouseState { NotPressed, Pressed, CtrlPressed, Dragging, CtrlDragging, Resizing };
	MouseState mouseState;

	Qt::CursorShape cursor;
#ifdef _WIN32
	bool dropShadow;
#endif
#ifdef __QT_SUPPORTS_SYSTEM_RESIZE
	Qt::Edges pressEdges;
#endif
	QPoint pressPosition;
	bool settingFlags;

	Qt::Edges getResizeEdges(const QPoint &position);
	Qt::CursorShape getCursor(const QPoint &position);
	void updateCursor(const QPoint &position);
	void updateCursor(Qt::CursorShape cursor);
	void clearCursor();

#ifdef _WIN32
	void setDropShadowInternal(bool value);
#endif
	void setTranslucent(bool value);
	void fixBounds();
	bool isOverTitleBar(const QPoint &point);
	bool shouldStartDrag(QMouseEvent *event);
	void temporarilyDisableAnimations();

private slots:
	void onVisibilityChanged(bool visible);
};
