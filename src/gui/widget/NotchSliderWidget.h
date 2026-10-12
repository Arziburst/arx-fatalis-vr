/*
 * Copyright 2026 Arx Fatalis VR contributors
 *
 * This file is part of Arx Fatalis VR, based on Arx Libertatis.
 *
 * Arx Libertatis is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Arx Libertatis is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Arx Libertatis.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef ARX_GUI_WIDGET_NOTCHSLIDERWIDGET_H
#define ARX_GUI_WIDGET_NOTCHSLIDERWIDGET_H

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gui/widget/TextWidget.h"
#include "gui/widget/Widget.h"
#include "math/Rectangle.h"

class Font;
class TextureContainer;

/*!
 * \brief A slider the player drags: a bar with notches and a dot that snaps to them (#260, D-NEW-turning)
 *
 * The label left, the bar, then the value's text. Pressing on the bar puts the dot on the nearest notch and
 * keeps dragging it while the button is held - the mouse in flat mode, a hand's reticle and trigger in VR
 * (D-016). The arrow keys and the wheel step one notch. Reusable: any setting with a few ordered values.
 */
class NotchSliderWidget final : public Widget {

public:

	/*!
	 * \param size the row's size (MenuPage::sliderSize)
	 * \param valueFont the font of the value's text, right of the bar
	 */
	NotchSliderWidget(const Vec2f & size, Font * font, std::string_view label, Font * valueFont);

	//! A notch and the text shown while the dot is on it, left to right
	void addNotch(std::string_view text);

	//! The notch the dot is on, without notifying (the page reading cfg.ini)
	void setValue(int value);
	
	//! The arrow keys' step: one notch left (\a delta -1) or right (+1), notifying
	void step(int delta) { newValue(m_value + delta); }

	int getValue() const { return m_value; }
	size_t notchCount() const { return m_notches.size(); }

	/*!
	 * Put the dot where the bar is at \a fraction (0 = left end, 1 = right end): the nearest notch, as a drag
	 * that lets go there does; notifies when the notch changes. For the test channel's \c menu \c drag.
	 */
	void dragTo(float fraction);

	bool click() override;
	void hover() override;
	void update() override;
	void render(bool mouseOver = false) override;
	void move(const Vec2f & offset) override;
	bool wantFocus() const override { return m_dragging; }
	void unfocus() override { m_dragging = false; }

	std::function<void(int /* notch */)> valueChanged;

	WidgetType type() const override {
		return WidgetType_NotchSlider;
	}

private:

	void newValue(int value);
	float cursorFraction() const;
	float notchX(size_t notch) const;

	std::unique_ptr<TextWidget> m_label;
	Font * m_valueFont;
	TextureContainer * m_dot;
	Rectf m_bar;   //!< where the dot can be (its centre runs from left to right)
	Rectf m_text;  //!< the value's text
	std::vector<std::string> m_notches;
	int m_value;
	bool m_dragging;

};

#endif // ARX_GUI_WIDGET_NOTCHSLIDERWIDGET_H
