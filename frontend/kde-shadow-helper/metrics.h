/*
 * SPDX-FileCopyrightText: 2014 Hugo Pereira Da Costa <hugo.pereira@free.fr>
 * SPDX-FileCopyrightText: 2020 Noah Davis <noahadvs@gmail.com>
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#pragma once

#include <QtGlobal>

namespace ShadowHelper {
//* standard pen widths
struct PenWidth {
	/* Using 1 instead of slightly more than 1 causes symbols drawn with
     * pen strokes to look skewed. The exact amount added does not matter
     * as long as it isn't too visible. Even with QPen::setCosmetic(true),
     * 1px pen widths still look slightly worse.
     */
	// The standard pen stroke width for symbols.
	static constexpr qreal Symbol = 1.001;

	// The standard pen stroke width for frames.
	static constexpr qreal Frame = 1.001;

	// The standard pen stroke width for shadows.
	static constexpr qreal Shadow = 1.001;

	// A value for pen width arguments to make it clear that there is no pen stroke
	static constexpr int NoPen = 0;
};

//* metrics
struct Metrics {
	// frames
	static constexpr int Frame_FrameWidth = 2;
	static constexpr int Frame_FrameRadius = 5;

	// shadow dimensions
	static constexpr int Shadow_Overlap = 2;
};
} // namespace ShadowHelper
