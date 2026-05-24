#include "OBSProxyStyle.hpp"
#include "moc_OBSProxyStyle.cpp"

#include <QStyleOption>
#include <QPainterPath>

#include <QAbstractItemView>
#include <QComboBox>
#include <QMenu>

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

	/* HACK ALERT:
	 * To allow for round corners on popup windows,
	 * WA_TranslucentBackground and FramelessWindowHint need to be set on widgets.
	 * There isn't an easy way to be notified when the widgets are created.
	 * Catching these particular hints is a decent workaround.
	 */

	case SH_Menu_Scrollable: {
		QMenu *menu = qobject_cast<QMenu *>(const_cast<QWidget *>(widget));
		if (!menu || menu->testAttribute(Qt::WA_TranslucentBackground))
			break;
		menu->setAttribute(Qt::WA_TranslucentBackground);
		menu->setWindowFlags(menu->windowFlags() | Qt::FramelessWindowHint);
		break;
	}

	case SH_ToolTipLabel_Opacity: {
		QWidget *toolTip = const_cast<QWidget *>(widget);
		if (toolTip->foregroundRole() != QPalette::ToolTipText ||
		    toolTip->testAttribute(Qt::WA_TranslucentBackground))
			break;
		toolTip->setAttribute(Qt::WA_TranslucentBackground);
		toolTip->setWindowFlags(toolTip->windowFlags() | Qt::FramelessWindowHint);
		break;
	}

	case SH_ComboBox_LayoutDirection: {
		QComboBox *comboBox = qobject_cast<QComboBox *>(const_cast<QWidget *>(widget));
		if (!comboBox)
			break;
		QAbstractItemView *itemView = comboBox->view();
		if (!itemView)
			break;
		QWidget *window = itemView->window();
		if (!window || window->testAttribute(Qt::WA_TranslucentBackground))
			break;
		window->setAttribute(Qt::WA_TranslucentBackground);
		window->setWindowFlags(window->windowFlags() | Qt::FramelessWindowHint);
		break;
	}

	default:
		break;
	}

	return QProxyStyle::styleHint(hint, option, widget, returnData);
}

int OBSInvisibleCursorProxyStyle::pixelMetric(PixelMetric metric, const QStyleOption *option,
					      const QWidget *widget) const
{

	if (metric == PM_TextCursorWidth)
		return 0;

	return QProxyStyle::pixelMetric(metric, option, widget);
}
