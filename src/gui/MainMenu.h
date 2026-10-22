/*
 * Copyright 2014-2022 Arx Libertatis Team (see the AUTHORS file)
 *
 * This file is part of Arx Libertatis.
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

#ifndef ARX_GUI_MAINMENU_H
#define ARX_GUI_MAINMENU_H

#include <memory>
#include <string>
#include <string_view>

#include "gui/MenuWidgets.h"
#include "gui/widget/Widget.h"
#include "gui/widget/WidgetContainer.h"
#include "math/Types.h"


class MainMenu {
	
public:
	
	bool bReInitAll;
	
	std::unique_ptr<MenuWindow> m_window;
	
	MainMenu(const MainMenu &) = delete;
	MainMenu & operator=(const MainMenu &) = delete;
	
	explicit MainMenu();
	~MainMenu();
	
	void init();
	void initWindowPages();
	
	void update();
	void render();
	
	void requestPage(MENUSTATE page) {
		m_requestedPage = page;
	}
	
	MENUSTATE requestedPage() const {
		return m_requestedPage;
	}

	//! The main menu's own entries (resume, new game, load / save, options, credits, quit, version, language)
	const WidgetContainer & widgets() const {
		return m_widgets;
	}

private:
	
	MENUSTATE m_requestedPage;
	
	TextureContainer * m_background;
	WidgetContainer m_widgets;
	
	TextWidget * m_resumeGame;
	
	Widget * m_selected;
	
};

extern MainMenu * g_mainMenu;

/*!
 * The next-launch crash notice (#545, M6): "The game ended with an error last time. A report was saved."
 * at the main menu, once per new report (platform/CrashNotice.h). The report it is about ("" = none),
 * whether it is on screen, and its two buttons - the test channel's `crashnotice ok|open` press them too.
 */
const std::string & crashNoticeReport();
bool crashNoticeShown();
void crashNoticeOk();
void crashNoticeOpen();

/*!
 * A click on the left (\a delta -1) or right (+1) arrow of a row of the VR turning page (#260) - turn,
 * angle, speed, repeat or height - for the test channel; the page must be open
 * \return an empty string, or why it was refused
 */
std::string stepVRSettingRow(std::string_view row, int delta);

//! The rows the VR turning page shows (e.g. "turn,angle,repeat,height"), empty when the page is not open (#374)
std::string visibleVRSettingRows();

/*!
 * The dot of a slider of the VR turning page (#260) - angle, speed or repeat - dragged to \a fraction of its
 * bar (0 left end, 1 right end) and let go there, for the test channel; the page must be open
 * \return an empty string, or why it was refused
 */
std::string dragVRSettingSlider(std::string_view row, float fraction);

#endif // ARX_GUI_MAINMENU_H
