
#pragma once

#include <QColor>

namespace ShadowHelper {

/* This code is normally auto-generated from data found in breeze.kcfg.
 * We simply set up some reasonable values by hand.
 * TODO: Provide a way to set these values.
 */
class StyleConfigData {

public:
	enum ShadowSize { ShadowNone, ShadowSmall, ShadowMedium, ShadowLarge, ShadowVeryLarge };

	static QColor shadowColor() { return QColor(0, 0, 0); }
	static ShadowSize shadowSize() { return ShadowSize::ShadowMedium; }
	static qreal shadowStrength() { return 2.0; }
};

} // namespace ShadowHelper
