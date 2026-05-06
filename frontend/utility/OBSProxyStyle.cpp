#include "OBSProxyStyle.hpp"
#include "moc_OBSProxyStyle.cpp"

#include <QStyleOption>
#include <QPainterPath>

void OBSProxyStyle::drawControl(ControlElement element, const QStyleOption *option, QPainter *painter,
				const QWidget *widget) const
{
	if (element == QStyle::CE_RubberBand) {
		// Use a simpler style for rubber bands
		QRectF rect(option->rect);

		static qreal borderThickness = 1;
		static qreal halfBorderThickness = borderThickness / 2;
		rect.adjust(halfBorderThickness, halfBorderThickness, -halfBorderThickness, -halfBorderThickness);

		QPainterPath path;
		path.addRoundedRect(rect, 2, 2);

		const QPalette *palette = &option->palette;
		QColor highlightColor = palette->color(QPalette::ColorGroup::Active, QPalette::Highlight);

#if true
		// Translucent
		QColor fillColor = QColor(highlightColor.red(), highlightColor.green(), highlightColor.blue(), 0x40);
#else
		// Opaque
		QColor windowColor = palette->color(QPalette::ColorGroup::Active, QPalette::Window);
		static float hcAmount = .25;
		static float wcAmount = 1 - hcAmount;
		QColor fillColor = QColor(windowColor.red() * wcAmount + highlightColor.red() * hcAmount,
					  windowColor.green() * wcAmount + highlightColor.green() * hcAmount,
					  windowColor.blue() * wcAmount + highlightColor.blue() * hcAmount);
#endif

		painter->setRenderHint(QPainter::Antialiasing);
		painter->setPen(QPen(highlightColor, borderThickness));
		painter->fillPath(path, fillColor);
		painter->drawPath(path);
		return;
	}

	QProxyStyle::drawControl(element, option, painter, widget);
}

/* The constants in the default QT styles don't dim the icons enough in
 * disabled mode
 *
 * https://code.woboq.org/qt5/qtbase/src/widgets/styles/qcommonstyle.cpp.html#6429
 */
QPixmap OBSProxyStyle::generatedIconPixmap(QIcon::Mode iconMode, const QPixmap &pixmap,
					   const QStyleOption *option) const
{
	if (iconMode == QIcon::Disabled) {
		QImage im = pixmap.toImage().convertToFormat(QImage::Format_ARGB32);

		// Cut the alpha in half for each pixel
		for (int y = 0; y < im.height(); ++y) {
			QRgb *scanLine = (QRgb *)im.scanLine(y);
			for (int x = 0; x < im.width(); ++x) {
				QRgb pixel = *scanLine;
				*scanLine = qRgba(qRed(pixel), qGreen(pixel), qBlue(pixel), qAlpha(pixel) / 2);
				++scanLine;
			}
		}

		return QPixmap::fromImage(im);
	}

	return QProxyStyle::generatedIconPixmap(iconMode, pixmap, option);
}

int OBSProxyStyle::styleHint(StyleHint hint, const QStyleOption *option, const QWidget *widget,
			     QStyleHintReturn *returnData) const
{
	switch (hint) {
	case SH_ComboBox_AllowWheelScrolling:
		return 0;
	case SH_Widget_Animation_Duration:
		// Make widget animations a little faster
		return 125;
#ifdef __APPLE__
	case SH_ComboBox_UseNativePopup:
		return 1;
#endif
	default:
		return QProxyStyle::styleHint(hint, option, widget, returnData);
	}
}

int OBSInvisibleCursorProxyStyle::pixelMetric(PixelMetric metric, const QStyleOption *option,
					      const QWidget *widget) const
{

	if (metric == PM_TextCursorWidth)
		return 0;

	return QProxyStyle::pixelMetric(metric, option, widget);
}
