#pragma once

#include "window-dock.hpp"
#include <QScopedPointer>

#include <browser-panel.hpp>
extern QCef *cef;
extern QCefCookieManager *panel_cookies;

class BrowserDock : public OBSDock {
private:
	QString title;

public:
	inline BrowserDock() : OBSDock() {
		// Don't set this any more, because CEF windows are container-ized
		//setAttribute(Qt::WA_NativeWindow);
	}
	inline BrowserDock(const QString &title_) : OBSDock(title_)
	{
		title = title_;
		//setAttribute(Qt::WA_NativeWindow);
	}

	QScopedPointer<QCefWidget> cefWidget;

	inline void SetWidget(QCefWidget *widget_)
	{
		setWidget(widget_);
		cefWidget.reset(widget_);
	}

	inline void setTitle(const QString &title_) { title = title_; }

	void closeEvent(QCloseEvent *event) override;
	void showEvent(QShowEvent *event) override;
};
