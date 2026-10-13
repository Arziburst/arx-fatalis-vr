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

#ifndef ARX_CORE_SAVEKIND_H
#define ARX_CORE_SAVEKIND_H

#include <string>
#include <string_view>

#include "io/fs/FilePath.h"
#include "platform/Platform.h"

class SaveBlock;

/*!
 * \brief VR saves and flat saves (#417, D-NEW): the two games never see each other's saves
 *
 * The VR game diverges from the flat one (its own mechanics, per-hand quick slots, VR state), so a save made in VR
 * carries a mark - one small file, \ref VRMarkFile, in the save block next to \c pld - and a run lists and loads
 * only the saves of its own kind: a VR run (an OpenXR session, the simulated headset included) only marked saves,
 * a flat run (\c --no-vr, or no runtime) only unmarked ones. Both directions are closed, nothing is imported
 * (owner, 2026-10-13). The mark is the only change to the save format.
 *
 * The decisions are pure and covered by tests/core/SaveKindTest, the mark's way through a save block too.
 */
enum class SaveKind : u8 {
	Flat, //!< no mark: made by flat Arx Libertatis, the original game, or this build without VR
	VR    //!< made while VR was active
};

//! "flat" or "vr" - for the log, game events and status.json
const char * saveKindName(SaveKind kind);

//! The save block file that marks a VR save
constexpr std::string_view VRMarkFile = "arxvr";

//! What a VR save's \ref VRMarkFile holds: a line a person can read when looking into a save
std::string vrMarkData();

//! The kind of a save whose \ref VRMarkFile holds \a markData: no mark (empty) is a flat save, any mark a VR one
SaveKind saveKindOfMark(std::string_view markData);

/*!
 * \return why a run of kind \a run must not list or load a save of kind \a save - an empty string when it may.
 *         The text names both kinds and the rule, for the log line of a refused load
 */
std::string saveKindRefusal(SaveKind save, SaveKind run);

//! Mark the save being written in \a block as a save of \a kind: a VR save gets the mark, a flat one loses it
bool writeSaveKind(SaveBlock & block, SaveKind kind);

//! The kind of the save in \a savefile, read from its save block (a save without the mark is flat)
SaveKind readSaveKind(const fs::path & savefile);

#endif // ARX_CORE_SAVEKIND_H
