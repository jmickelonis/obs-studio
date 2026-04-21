#include "VolumeSlider.hpp"

#include <QPainter>

#include "moc_VolumeSlider.cpp"

VolumeSlider::VolumeSlider(obs_fader_t *fader, QWidget *parent) : AbsoluteSlider(parent)
{
	fad = fader;
}

VolumeSlider::VolumeSlider(obs_fader_t *fader, Qt::Orientation orientation, QWidget *parent)
	: AbsoluteSlider(orientation, parent)
{
	fad = fader;
}

bool VolumeSlider::getDisplayTicks() const
{
	return displayTicks;
}

void VolumeSlider::setDisplayTicks(bool display)
{
	displayTicks = display;
}

void VolumeSlider::paintEvent(QPaintEvent *event)
{
	if (!getDisplayTicks()) {
		QSlider::paintEvent(event);
		return;
	}

	QPainter painter(this);
	QColor tickColor = getTickColor();

	obs_fader_conversion_t fader_db_to_def = obs_fader_db_to_def(fad);

	QStyleOptionSlider opt;
	initStyleOption(&opt);

	QRect groove = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
	QRect handle = style()->subControlRect(QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);

	if (orientation() == Qt::Horizontal) {
		const int sliderWidth = groove.width() - handle.width();

		int n = std::ceil((groove.top() - handle.top()) / 2.0);
		int yPos = groove.top() - n;
		int tickLength = groove.height() + n * 2;

		for (int db = -10; db >= -90; db -= 10) {
			float tickValue = fader_db_to_def(db);

			float xPos = groove.left() + (tickValue * sliderWidth) + (handle.width() / 2);
			painter.fillRect(xPos, yPos, 1, tickLength, tickColor);
		}
	} else {
		const int sliderHeight = groove.height() - handle.height();

		int n = std::ceil((groove.left() - handle.left()) / 2.0);
		int xPos = groove.left() - n;
		int tickLength = groove.width() + n * 2;

		for (int db = -10; db >= -90; db -= 10) {
			float tickValue = fader_db_to_def(db);

			float yPos =
				groove.height() + groove.top() - (tickValue * sliderHeight) - (handle.height() / 2);
			painter.fillRect(xPos, yPos, tickLength, 1, tickColor);
		}
	}

	QSlider::paintEvent(event);
}
