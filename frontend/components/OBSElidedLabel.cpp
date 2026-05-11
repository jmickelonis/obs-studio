
#include "OBSElidedLabel.hpp"

#include <QStyleOptionFrame>
#include <QStylePainter>

OBSElidedLabel::OBSElidedLabel(QWidget *parent, Qt::WindowFlags f) : QLabel(parent, f) {}
OBSElidedLabel::OBSElidedLabel(const QString &text, QWidget *parent, Qt::WindowFlags f) : QLabel(text, parent, f) {}

void OBSElidedLabel::setText(const QString &text)
{
	QLabel::setText(text);
	updateElidedText();
}

void OBSElidedLabel::resizeEvent(QResizeEvent *event)
{
	updateElidedText();
}

void OBSElidedLabel::paintEvent(QPaintEvent *event)
{
	QStylePainter p(this);
	QStyleOption opt;
	opt.initFrom(this);
	p.drawPrimitive(QStyle::PE_Widget, opt);
	p.drawText(elidedTextBounds.x(), elidedTextBounds.y(), elidedTextBounds.width(), elidedTextBounds.height(),
		   alignment(), elidedText);
}

void OBSElidedLabel::updateElidedText()
{
	QStyleOptionFrame opt;
	opt.initFrom(this);
	elidedTextBounds = style()->subElementRect(QStyle::SE_FrameContents, &opt, this);

	const QString &text = this->text();
	elidedText = fontMetrics().elidedText(text, Qt::TextElideMode::ElideRight, elidedTextBounds.width(),
					      Qt::TextShowMnemonic);

    // Provide a tooltip with the full text if necessary
	setToolTip(elidedText != text ? text : "");
}
