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

#include "gui/widget/NotchSliderWidget.h"

#include <algorithm>
#include <utility>

#include "graphics/Draw.h"
#include "graphics/DrawLine.h"
#include "graphics/Renderer.h"
#include "graphics/data/TextureContainer.h"
#include "gui/Text.h"
#include "input/Input.h"
#include "vr/VRSettingsRows.h"

namespace {

//! The menu's text colour (TextWidget)
const Color NotchColor = Color(232, 204, 142);

} // anonymous namespace

NotchSliderWidget::NotchSliderWidget(const Vec2f & size, Font * font, std::string_view label, Font * valueFont)
	: m_label(std::make_unique<TextWidget>(font, label))
	, m_valueFont(valueFont)
	, m_dot(TextureContainer::Load("graph/interface/menus/menu_slider_on"))
	, m_value(0)
	, m_dragging(false)
{

	m_label->forceDisplay(TextWidget::Dynamic);

	m_rect = Rectf(size.x, std::max(size.y, m_label->m_rect.height()));

	// Right to left: the value's text, a gap, the bar; the dot overhangs the bar by half its size at each end
	const float textWidth = 3.f * size.y;
	const float barWidth = 4.f * size.y;
	m_text = Rectf(Vec2f(m_rect.right - textWidth, m_rect.top), textWidth, m_rect.height());
	m_bar = Rectf(Vec2f(m_text.left - 0.75f * size.y - barWidth, m_rect.top), barWidth, m_rect.height());

	m_label->setPosition(Vec2f(m_rect.left, m_rect.center().y - m_label->m_rect.height() / 2));

}

void NotchSliderWidget::addNotch(std::string_view text) {
	m_notches.emplace_back(text);
}

void NotchSliderWidget::setValue(int value) {
	m_value = m_notches.empty() ? 0 : std::clamp(value, 0, int(m_notches.size()) - 1);
}

void NotchSliderWidget::dragTo(float fraction) {
	newValue(int(vr::notchAt(fraction, m_notches.size())));
}

void NotchSliderWidget::move(const Vec2f & offset) {
	Widget::move(offset);
	m_label->move(offset);
	m_bar.move(offset);
	m_text.move(offset);
}

float NotchSliderWidget::cursorFraction() const {
	const float x = float(GInput->getMousePosition().x);
	return (x - m_bar.left) / std::max(m_bar.width(), 1.f);
}

float NotchSliderWidget::notchX(size_t notch) const {
	return m_bar.left + vr::notchFraction(notch, m_notches.size()) * m_bar.width();
}

bool NotchSliderWidget::click() {

	bool result = Widget::click();

	if(!m_enabled) {
		return result;
	}

	// The bar and the dot overhanging its ends grab; the label and the text do nothing
	const Vec2f cursor = Vec2f(GInput->getMousePosition());
	Rectf grab = m_bar;
	grab.left -= m_rect.height() / 2;
	grab.right += m_rect.height() / 2;
	if(grab.contains(cursor)) {
		m_dragging = true;
		dragTo(cursorFraction());
		return true;
	}

	return result;
}

void NotchSliderWidget::update() {
	if(!m_dragging) {
		return;
	}
	if(!m_enabled || !GInput->getMouseButtonRepeat(Mouse::Button_0)) {
		m_dragging = false;
		return;
	}
	dragTo(cursorFraction());
}

void NotchSliderWidget::hover() {
	if(GInput->isKeyPressedNowPressed(Keyboard::Key_LeftArrow) || GInput->getMouseWheelDir() < 0) {
		newValue(m_value - 1);
	} else if(GInput->isKeyPressedNowPressed(Keyboard::Key_RightArrow) || GInput->getMouseWheelDir() > 0) {
		newValue(m_value + 1);
	}
}

void NotchSliderWidget::render(bool mouseOver) {

	m_label->render(mouseOver || m_dragging);

	const Color color = m_enabled ? NotchColor : Color::gray(0.25f);
	const float y = m_bar.center().y;
	const float h = m_rect.height();

	// The bar: two lines so it stays visible on the VR panel
	drawLine(Vec2f(m_bar.left, y - 0.5f), Vec2f(m_bar.right, y - 0.5f), 0.f, color);
	drawLine(Vec2f(m_bar.left, y + 0.5f), Vec2f(m_bar.right, y + 0.5f), 0.f, color);

	for(size_t i = 0; i < m_notches.size(); i++) {
		const float x = notchX(i);
		drawLine(Vec2f(x, y - h * 0.22f), Vec2f(x, y + h * 0.22f), 0.f, color);
	}

	if(!m_notches.empty()) {
		const float size = h * 0.8f;
		const Rectf dot(Vec2f(notchX(size_t(m_value)) - size / 2, y - size / 2), size, size);
		UseRenderState state(render2D().blendAdditive());
		EERIEDrawBitmap(dot, 0, m_dot, m_enabled ? Color::white : Color::gray(0.25f));
		if(mouseOver || m_dragging) {
			EERIEDrawBitmap(dot, 0, m_dot, Color::white);
		}
		drawTextCentered(m_valueFont, m_text.center(), m_notches[size_t(m_value)], color);
	}

}

void NotchSliderWidget::newValue(int value) {

	if(m_notches.empty()) {
		return;
	}

	value = std::clamp(value, 0, int(m_notches.size()) - 1);
	if(value == m_value) {
		return;
	}

	m_value = value;

	if(valueChanged) {
		valueChanged(m_value);
	}

}
