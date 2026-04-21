/******************************************************************************
    Copyright (C) 2025 by Taylor Giampaolo <warchamp7@obsproject.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "VolumeName.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QStyle>
#include <QStyleOptionButton>
#include <QStylePainter>

#include "moc_VolumeName.cpp"

namespace {
QString getPlainText(const QString &text)
{
	if (Qt::mightBeRichText(text)) {
		QTextDocument doc;
		doc.setHtml(text);

		return doc.toPlainText();
	}

	return text;
}
} // namespace

VolumeLabel::VolumeLabel(QWidget *parent) : QLabel(parent)
{
	direction = Up;
}

VolumeLabel::Direction VolumeLabel::getDirection()
{
	return direction;
}

void VolumeLabel::setDirection(Direction value)
{
	Direction old = direction;
	if (value == old)
		return;
	direction = value;
	update();
}

QSize VolumeLabel::sizeHint() const
{
	const QSize size = QLabel::sizeHint();
	switch (direction) {
	case Left:
	case Right:
		QMargins margins = contentsMargins();
		int mw = margins.left() + margins.right();
		int mh = margins.top() + margins.bottom();
		return QSize(size.height() - mh + mw, size.width() - mw + mh);
	}
	return size;
}

QSize VolumeLabel::minimumSizeHint() const
{
	const QSize size = QLabel::minimumSizeHint();
	switch (direction) {
	case Left:
	case Right:
		QMargins margins = contentsMargins();
		int mw = margins.left() + margins.right();
		int mh = margins.top() + margins.bottom();
		return QSize(size.height() - mh + mw, size.width() - mw + mh);
	}
	return size;
}

void VolumeLabel::paintEvent(QPaintEvent *event)
{
	QStyleOption opt;
	opt.initFrom(this);

	QStylePainter painter(this);
	painter.drawPrimitive(QStyle::PE_Widget, opt);

	QMargins margins = contentsMargins();
	painter.save();
	painter.translate(margins.left(), margins.top());

	QRect bounds = rect();
	bounds.setSize(QSize(bounds.width() - (margins.left() + margins.right()),
			     bounds.height() - (margins.top() + margins.bottom())));

	switch (direction) {
	case Left:
		bounds = bounds.transposed();
		painter.rotate(-90);
		painter.translate(-bounds.width(), 0);
		break;
	case Right:
		bounds = bounds.transposed();
		painter.rotate(90);
		painter.translate(0, -bounds.height());
		break;
	case Down:
		painter.rotate(180);
		painter.translate(-bounds.width(), -bounds.height());
		break;
	default:
		break;
	}

	painter.drawText(bounds, alignment(), text());
	painter.restore();
}

VolumeNameLayout::VolumeNameLayout() : QHBoxLayout() {}

void VolumeNameLayout::setGeometry(const QRect &r)
{
	VolumeName *name = qobject_cast<VolumeName *>(parentWidget());

	QStyleOptionButton opt;
	opt.initFrom(name);

	QRect bounds = name->style()->subElementRect(QStyle::SE_PushButtonContents, &opt, name);
	if (name->vertical)
		bounds.setTop(bounds.top() + name->indicatorWidth);
	else
		bounds.setRight(bounds.right() - name->indicatorWidth);
	name->label->setGeometry(bounds);
}

VolumeName::VolumeName(obs_source_t *source, QWidget *parent) : QAbstractButton(parent)
{
	renamedSignal = OBSSignal(obs_source_get_signal_handler(source), "rename", &VolumeName::obsSourceRenamed, this);
	removedSignal = OBSSignal(obs_source_get_signal_handler(source), "remove", &VolumeName::obsSourceRemoved, this);
	destroyedSignal =
		OBSSignal(obs_source_get_signal_handler(source), "destroy", &VolumeName::obsSourceDestroyed, this);

	VolumeNameLayout *layout = new VolumeNameLayout();
	setLayout(layout);

	label = new VolumeLabel(this);
	label->setIndent(0);
	layout->addWidget(label);

	layout->setContentsMargins(0, 0, 0, 0);

	QString name = obs_source_get_name(source);
	setText(name);

	vertical = false;
	indicatorWidth = 0;
}

VolumeName::~VolumeName() {}

void VolumeName::setAlignment(Qt::Alignment alignment_)
{
	if (textAlignment != alignment_) {
		textAlignment = alignment_;
		update();
	}
}

void VolumeName::setVertical(bool value)
{
	bool old = vertical;
	if (value == old)
		return;
	vertical = value;
	label->setDirection(vertical ? VolumeLabel::Direction::Left : VolumeLabel::Direction::Up);
	update();
}

QSize VolumeName::sizeHint() const
{
	QString plainText = getPlainText(fullText);

	QFontMetrics metrics(label->font());

	int width = metrics.horizontalAdvance(plainText) + indicatorWidth;
	int height = metrics.height();

	if (vertical)
		std::swap(width, height);

	QMargins margins = label->contentsMargins();
	QSize size = QSize(width, height).grownBy(margins);

	QStyleOptionButton opt;
	opt.initFrom(this);
	return style()->sizeFromContents(QStyle::CT_PushButton, &opt, size, this);
}

void VolumeName::obsSourceRenamed(void *data, calldata_t *params)
{
	VolumeName *widget = static_cast<VolumeName *>(data);
	const char *name = calldata_string(params, "new_name");

	QMetaObject::invokeMethod(widget, "onRenamed", Qt::QueuedConnection, Q_ARG(QString, name));
}

void VolumeName::obsSourceRemoved(void *data, calldata_t *)
{
	VolumeName *widget = static_cast<VolumeName *>(data);

	QMetaObject::invokeMethod(widget, "onRemoved", Qt::QueuedConnection);
}

void VolumeName::obsSourceDestroyed(void *data, calldata_t *)
{
	VolumeName *widget = static_cast<VolumeName *>(data);

	QMetaObject::invokeMethod(widget, "onDestroyed", Qt::QueuedConnection);
}

void VolumeName::resizeEvent(QResizeEvent *event)
{
	updateLabelText(text());
	QAbstractButton::resizeEvent(event);
}

void VolumeName::paintEvent(QPaintEvent *)
{
	QStylePainter painter(this);
	QStyleOptionButton opt;
	opt.initFrom(this);

	painter.drawControl(QStyle::CE_PushButtonBevel, opt);

	QRect contentRect = style()->subElementRect(QStyle::SE_PushButtonContents, &opt, this);

	painter.save();
	QPointF center = contentRect.center();
	if (vertical) {
		painter.translate((int)(center.x() - indicatorWidth / 2.0), contentRect.top());
		painter.rotate(-90);
		painter.translate(-indicatorWidth, 0);
	} else {
		painter.translate(contentRect.x() + contentRect.width() - indicatorWidth,
				  (int)(center.y() - indicatorWidth / 2.0));
	}

	QStyleOption arrowOpt;
	arrowOpt.initFrom(label);
	arrowOpt.rect = QRect(0, 0, indicatorWidth, indicatorWidth);
	painter.drawPrimitive(QStyle::PE_IndicatorArrowDown, arrowOpt);
	painter.restore();
}

bool VolumeName::event(QEvent *event)
{
	if (event->type() == QEvent::StyleChange) {
		indicatorWidth = style()->pixelMetric(QStyle::PM_MenuButtonIndicator, nullptr, label);
		update();
	}

	return QAbstractButton::event(event);
}

void VolumeName::onRenamed(QString name)
{
	setText(name);

	std::string nameStr = name.toStdString();
	emit renamed(nameStr.c_str());
}

void VolumeName::setText(const QString &text)
{
	QAbstractButton::setText(text);
	updateGeometry();
	updateLabelText(text);
}

void VolumeName::updateLabelText(const QString &name)
{
	QString plainText = getPlainText(name);

	QFontMetrics metrics(label->font());

	QRect contentsRect = label->contentsRect();
	int availableWidth = vertical ? contentsRect.height() : contentsRect.width();
	if (availableWidth <= 0) {
		label->clear();
		fullText = name;
		setToolTip(plainText);
		return;
	}

	int textWidth = metrics.horizontalAdvance(plainText);

	bool isRichText = (plainText != name);
	bool needsElide = textWidth > availableWidth;

	if (needsElide && !isRichText) {
		QString elided = metrics.elidedText(plainText, Qt::ElideRight, availableWidth);
		label->setText(elided);
		setToolTip(plainText);
	} else {
		label->setText(name);
		setToolTip("");
	}

	fullText = name;
}

void VolumeName::onRemoved()
{
	emit removed();
}

void VolumeName::onDestroyed()
{
	emit destroyed();
}
