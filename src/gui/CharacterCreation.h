/*
 * Copyright 2017-2019 Arx Libertatis Team (see the AUTHORS file)
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

#ifndef ARX_GUI_CHARACTERCREATION_H
#define ARX_GUI_CHARACTERCREATION_H

#include <string>
#include <string_view>

#include "math/Rectangle.h"
#include "platform/Platform.h"

class TextureContainer;

struct CharacterCreation {

	CharacterCreation();

	void loadData();
	void freeData();

	void resetCheat();
	void render();

	/*!
	 * For the test channel (#394): where the button (quickgen, skin, done) or the attribute or skill of the
	 * book (\ref StatsPage::slotRect) named \a name is clicked on the interface, as the last frame drew it
	 * \return false for an unknown name
	 */
	bool targetRect(std::string_view name, Rectf & rect) const;

private:
	Rectf m_quickGenerateButton;
	Rectf m_skinButton;
	Rectf m_doneButton;

	TextureContainer * BookBackground;
	std::string str_button_quickgen;
	std::string str_button_skin;
	std::string str_button_done;
	
	std::string m_desc_quickgen;
	std::string m_desc_skin;
	std::string m_desc_done;
	
	s8 m_cheatSkinButtonClickCount;
	char m_cheatQuickGenButtonClickCount;
};

extern CharacterCreation g_characterCreation;

#endif // ARX_GUI_CHARACTERCREATION_H
