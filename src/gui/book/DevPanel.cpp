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

#include "gui/book/DevPanel.h"

#include <algorithm>
#include <string>

#include "core/Core.h"
#include "game/Player.h"
#include "graphics/Draw.h"
#include "graphics/DrawLine.h"
#include "graphics/font/Font.h"
#include "gui/Cursor.h"
#include "gui/Text.h"
#include "gui/book/Book.h"
#include "input/Input.h"
#include "scene/GameSound.h"

namespace devvariants {

namespace {

bool g_showMap = false; //!< Show map was pressed with no group registered; reset when the book closes
size_t g_page = 0;      //!< The panel's page asked for
Rectf g_area;           //!< Where the panel was last drawn, for the test channel's targets
float g_row = 0.f;

const float PanelZ = 0.000001f; // in front of the book's page, as its other items

const Color PageText = Color::black;
const Color TitleText = Color(110, 20, 10);
const Color ButtonFill = Color(226, 212, 176);
const Color CurrentFill = Color(110, 60, 25);
const Color CurrentText = Color::white;
const Color HoverFill = Color(240, 232, 205);
const Color Outline = Color(60, 40, 20);

//! The panel's area in the book: both pages inside their margins, in book units scaled
Rectf panelArea(const Rectf & bookRect, float scale) {
	return Rectf(bookRect.topLeft() + Vec2f(44.f, 26.f) * scale, bookRect.topLeft() + Vec2f(482.f, 290.f) * scale);
}

float rowHeight(float scale) {
	const float line = hFontInBook ? float(hFontInBook->getLineHeight()) : 14.f * scale;
	return std::max(line + 6.f * scale, 20.f * scale);
}

Layout currentLayout() {
	TextWidth width;
	if(hFontInBook) {
		width = [](std::string_view text) { return float(hFontInBook->getTextSize(text).advance()); };
	}
	return layout(g_area, g_row, g_page, width);
}

void drawText(const Rectf & rect, std::string_view text, Color color, bool centered) {
	if(!hFontInBook || text.empty()) {
		return;
	}
	const float width = float(hFontInBook->getTextSize(text).advance());
	const float x = centered ? rect.center().x - width / 2.f : rect.left;
	const float y = rect.center().y - float(hFontInBook->getLineHeight()) / 2.f;
	hFontInBook->draw(Vec2i(Vec2f(x, y)), text, color);
}

bool mouseIn(const Rectf & rect) {
	return rect.contains(Vec2f(DANAEMouse));
}

//! Draws a button; true when it was clicked this frame
bool manageButton(const Button & button) {
	const bool hover = mouseIn(button.rect);
	EERIEDrawBitmap(button.rect, PanelZ, nullptr, button.current ? CurrentFill : (hover ? HoverFill : ButtonFill));
	drawLineRectangle(button.rect, PanelZ, Outline);
	drawText(button.rect, button.label, button.current ? CurrentText : PageText, true);
	if(!hover) {
		return false;
	}
	cursorSetInteraction();
	// A second press soon after the first comes as a double click (bit 4, not 1): - / + pressed quickly count each
	return eeMouseDown1() || eeMouseDoubleClick1();
}

void press(const Button & button) {
	switch(button.kind) {
		case ButtonKind::Choice: {
			choose(button.group, button.index);
			ARX_SOUND_PlayMenu(g_snd.MENU_CLICK);
			break;
		}
		case ButtonKind::Minus: {
			step(button.group, -1);
			ARX_SOUND_PlayMenu(g_snd.MENU_CLICK);
			break;
		}
		case ButtonKind::Plus: {
			step(button.group, 1);
			ARX_SOUND_PlayMenu(g_snd.MENU_CLICK);
			break;
		}
		case ButtonKind::PrevPage: {
			g_page = (g_page > 0) ? g_page - 1 : 0;
			ARX_SOUND_PlayInterface(g_snd.BOOK_PAGE_TURN);
			break;
		}
		case ButtonKind::NextPage: {
			g_page++;
			ARX_SOUND_PlayInterface(g_snd.BOOK_PAGE_TURN);
			break;
		}
		case ButtonKind::ShowMap: {
			g_showMap = true;
			ARX_SOUND_PlayInterface(g_snd.BOOK_PAGE_TURN);
			break;
		}
	}
}

} // anonymous namespace

bool manageMapPage(const Rectf & bookRect, float scale) {

	if(mapPageView(g_devLaunch, count(), g_showMap) == MapPageView::Map) {
		return false;
	}

	g_area = panelArea(bookRect, scale);
	g_row = rowHeight(scale);
	const Layout panel = currentLayout();
	g_page = panel.page;

	if(panel.empty) {
		drawText(Rectf(g_area.left, g_area.top, g_area.right, g_area.top + g_row), panel.notice, PageText, false);
	}

	for(const BlockLayout & block : panel.blocks) {
		drawText(block.title, "PR " + block.prLabel, TitleText, false);
		drawLine(Vec2f(block.title.left, block.title.bottom - 1.f), Vec2f(block.title.right, block.title.bottom - 1.f),
		         PanelZ, TitleText);
	}

	for(const GroupLayout & group : panel.groups) {
		const std::string label = group.afterRestart ? group.group + "  (after restart)" : group.group;
		drawText(group.label, label, PageText, false);
		if(!group.value.empty()) {
			drawText(group.value, group.valueText, PageText, true);
		}
	}

	if(panel.pages > 1) {
		const Rectf footer(g_area.left, g_area.bottom - g_row, g_area.right, g_area.bottom);
		drawText(footer, std::to_string(panel.page + 1) + " / " + std::to_string(panel.pages), PageText, true);
	}

	// One press a frame: a button acts on what was drawn
	const Button * pressed = nullptr;
	for(const Button & button : panel.buttons) {
		if(manageButton(button) && !pressed) {
			pressed = &button;
		}
	}
	if(pressed) {
		press(*pressed);
	}

	return true;
}

void onBookClosed() {
	g_showMap = false;
	g_page = 0;
}

PanelStatus panelStatus() {
	PanelStatus status;
	status.view = mapPageView(g_devLaunch, count(), g_showMap);
	status.visible = status.view != MapPageView::Map && (player.Interface & INTER_PLAYERBOOK)
	                 && g_playerBook.currentPage() == BOOKMODE_MINIMAP;
	if(status.view == MapPageView::Panel && g_row > 0.f) {
		const Layout panel = currentLayout();
		status.page = panel.page;
		status.pages = panel.pages;
	}
	return status;
}

const char * mapPageViewName(MapPageView view) {
	switch(view) {
		case MapPageView::Map:   return "map";
		case MapPageView::Panel: return "panel";
		case MapPageView::Empty: return "empty";
	}
	return "?";
}

bool panelTargetRect(std::string_view target, Rectf & rect) {
	if(!panelStatus().visible || g_row <= 0.f) {
		return false;
	}
	for(const Button & button : currentLayout().buttons) {
		if(button.target == target) {
			rect = button.rect;
			return true;
		}
	}
	return false;
}

} // namespace devvariants
