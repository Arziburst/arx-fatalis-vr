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

#ifndef ARX_GUI_WIDGET_VRBINDINGWIDGET_H
#define ARX_GUI_WIDGET_VRBINDINGWIDGET_H

#include <string>

#include "gui/widget/Widget.h"
#include "vr/VRControls.h"

class Font;

/*!
 * \brief One event's binding on the VR settings page (#178), like the game's own KeybindWidget
 *
 * Shows what triggers the event on the current controllers; clicking it starts capturing what the
 * player presses (vr::startCapture) and blinks until the capture ends. Shown in red while another
 * event shares the binding (D-020: conflicts are shown, both fire).
 */
class VRBindingWidget final : public Widget {

	vr::Event m_event;
	Font * m_font;
	std::string m_text;
	bool m_conflict;

	void setText(const std::string & text);

public:

	VRBindingWidget(vr::Event event, Font * font);

	bool click() override;
	void update() override;
	void render(bool mouseOver = false) override;
	bool wantFocus() const override;
	void unfocus() override;

	WidgetType type() const override {
		return WidgetType_VRBinding;
	}

	vr::Event event() const { return m_event; }

};

/*!
 * \brief The VR settings page's help line (#178): the controllers, what to press while capturing,
 *        a refused control and why, the conflicts
 */
class VRControlsInfoWidget final : public Widget {

	Font * m_font;
	float m_width;
	std::string m_text;
	bool m_warning;

public:

	VRControlsInfoWidget(Font * font, float width, int lines);

	void update() override;
	void render(bool mouseOver = false) override;

	WidgetType type() const override {
		return WidgetType_VRInfo;
	}

	const std::string & text() const { return m_text; }

};

#endif // ARX_GUI_WIDGET_VRBINDINGWIDGET_H
