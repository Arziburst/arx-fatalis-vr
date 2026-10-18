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

#include "gui/widget/VRBindingWidget.h"

#include <vector>

#include "core/Application.h"
#include "core/GameTime.h"
#include "core/TimeTypes.h"
#include "graphics/Color.h"
#include "graphics/font/Font.h"
#include "gui/Text.h"
#include "input/Input.h"
#include "platform/Time.h"
#include "vr/BindingCapture.h"
#include "vr/VRInput.h"
#include "vr/VRSystem.h"
#include "window/RenderWindow.h"

namespace {

bool capturing(vr::Event event) {
	return vr::capture().active() && vr::captureEvent() == event;
}

bool inConflict(vr::Event event) {
	for(const vr::Conflict & conflict : vr::findConflicts(vr::controls().layout())) {
		if(conflict.first == event || conflict.second == event) {
			return true;
		}
	}
	return false;
}

// The colours of the game's own menus (TextWidget, KeybindWidget)
const Color MenuText = Color(232, 204, 142);
const Color ConflictText = Color(232, 96, 64);

} // anonymous namespace

VRBindingWidget::VRBindingWidget(vr::Event event, Font * font)
	: m_event(event)
	, m_font(font)
	, m_conflict(false)
{
	setText("---");
}

void VRBindingWidget::setText(const std::string & text) {
	if(text == m_text) {
		return;
	}
	m_text = text;
	Vec2i textSize = m_font->getTextSize(m_text);
	m_rect = Rectf(m_rect.topLeft(), float(textSize.x + 1), float(textSize.y + 1));
}

bool VRBindingWidget::click() {

	bool result = Widget::click();

	// Remapping is done with the controllers; without a headset there are none to press
	if(!m_enabled || !vr::isEnabled()) {
		return result;
	}

	if(!capturing(m_event)) {
		vr::startCapture(m_event);
	}

	return true;
}

void VRBindingWidget::update() {

	if(capturing(m_event)) {
		if(GInput->isKeyPressedNowPressed(Keyboard::Key_Escape)) {
			vr::cancelCapture();
		} else {
			const vr::BindingCapture & capture = vr::capture();
			if(capture.state() == vr::BindingCapture::State::Holding) {
				vr::Binding held = capture.binding();
				held.longPress = capture.heldLong(toMsi(platform::getTime()));
				setText(vr::bindingLabel(held, vr::controllerType()) + "...");
			} else {
				setText("...");
			}
			return;
		}
	}

	m_conflict = inConflict(m_event);
	setText(vr::bindingLabel(vr::controls().layout()[m_event], vr::controllerType()));
}

void VRBindingWidget::render(bool mouseOver) {

	Color color = m_conflict ? ConflictText : MenuText;
	if(capturing(m_event)) {
		bool blink = true;
		if(mainApp->getWindow()->hasFocus()) {
			blink = timeWaveSquare(g_platformTime.frameStart(), 400ms);
		}
		color = blink ? Color::white : (Color::red * 0.2f);
	} else if(!m_enabled) {
		color = Color::gray(0.5f);
	} else if(mouseOver && vr::isEnabled()) {
		color = Color::white;
	}

	ARX_UNICODE_DrawTextInRect(m_font, m_rect.topLeft(), m_rect.right, m_text, color, nullptr);
}

bool VRBindingWidget::wantFocus() const {
	return capturing(m_event);
}

void VRBindingWidget::unfocus() {
	if(capturing(m_event)) {
		vr::cancelCapture();
	}
}

VRControlsInfoWidget::VRControlsInfoWidget(Font * font, float width, int lines)
	: m_font(font)
	, m_width(width)
	, m_warning(false)
{
	m_rect = Rectf(m_width, float(m_font->getLineHeight() * lines));
	setEnabled(false);
}

void VRControlsInfoWidget::update() {

	const vr::ControllerType type = vr::controllerType();
	const vr::BindingCapture & capture = vr::capture();
	const vr::ControlLayout & layout = vr::controls().layout();
	m_warning = false;

	if(capture.active()) {
		std::string text = std::string("Press what should trigger \"") + vr::eventLabel(vr::captureEvent())
		                   + "\": a button; hold it for a long press; hold it and press a second button of"
		                     " the same hand for a chord.";
		if(capture.refused() != vr::Binding::None) {
			text += ' ' + vr::controlLabel(capture.refused(), type) + ": " + capture.refusedWhy() + '.';
			m_warning = true;
		}
		m_text = text;
		return;
	}

	if(!vr::remapNote().empty()) {
		m_text = vr::remapNote();
		m_warning = true;
		return;
	}

	const std::vector<vr::Conflict> conflicts = vr::findConflicts(layout);
	if(!conflicts.empty()) {
		const vr::Conflict & conflict = conflicts.front();
		m_text = std::string("Conflict: \"") + vr::eventLabel(conflict.first) + "\" and \""
		         + vr::eventLabel(conflict.second) + "\" share " + vr::bindingLabel(layout[conflict.first], type)
		         + " - both fire.";
		if(conflicts.size() > 1) {
			m_text += " (" + std::to_string(conflicts.size()) + " conflicts)";
		}
		m_warning = true;
		return;
	}

	if(!vr::isEnabled()) {
		m_text = "Remapping uses the VR controllers: start the game with SteamVR running.";
		return;
	}

	m_text = std::string("Controllers: ") + vr::controllerTypeName(vr::layoutType(type))
	         + (vr::layoutRemapped() ? " (remapped)" : " (default layout)")
	         + ". Click a binding with either trigger to change it.";
}

void VRControlsInfoWidget::render(bool /* mouseOver */) {
	const Color color = m_warning ? ConflictText : Color::gray(0.75f);
	ARX_UNICODE_DrawTextInRect(m_font, m_rect.topLeft(), m_rect.left + m_width, m_text, color, nullptr);
}
