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

#ifndef ARX_CORE_SAVESLOTS_H
#define ARX_CORE_SAVESLOTS_H

#include <string>
#include <string_view>
#include <vector>

/*!
 * \file
 * \brief Which folders of the save directory are the player's saves (#550)
 *
 * The replay recorder (vr/ReplayRecorder.h) saves the game at every VR session's record start into a replay slot,
 * save9000 .. save9004 (the oldest reused, five kept). Those slots are never the player's: the load menu does not
 * list them, --loadslot never counts them and quickload never picks one - whether or not any other save exists.
 * A replay starts from such a save by copying it into its own user directory as an ordinary slot (replay.ps1).
 * Pure functions, host-tested (tests/core/SaveSlotsTest.cpp); core/SaveGame.cpp applies them.
 */

//! The replay recorder's slots: save9000 .. save9004
inline bool isReplaySaveSlot(std::string_view folder) {
	return folder.size() == 8 && folder.substr(0, 4) == "save" && folder.substr(4, 4) >= "9000" && folder.substr(4, 4) <= "9004";
}

//! The folders of \a folders the save list takes, in their order: everything but the replay slots
inline std::vector<std::string> listedSaveFolders(const std::vector<std::string> & folders) {
	std::vector<std::string> listed;
	for(const std::string & folder : folders) {
		if(!isReplaySaveSlot(folder)) {
			listed.push_back(folder);
		}
	}
	return listed;
}

#endif // ARX_CORE_SAVESLOTS_H
