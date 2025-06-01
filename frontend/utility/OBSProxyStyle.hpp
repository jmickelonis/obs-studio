#pragma once

#include <QProxyStyle>
#include <QPainter>

class OBSProxyStyle : public QProxyStyle {
	Q_OBJECT

public:
	OBSProxyStyle() : QProxyStyle() {}

	OBSProxyStyle(const QString &key) : QProxyStyle(key) {}

	int styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
		      QStyleHintReturn *returnData) const override;
	QPixmap generatedIconPixmap(QIcon::Mode iconMode, const QPixmap &pixmap,
				    const QStyleOption *option) const override;
	void drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
			 const QWidget *widget) const override;
};
