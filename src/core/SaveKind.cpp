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

#include "core/SaveKind.h"

#include "io/SaveBlock.h"

const char * saveKindName(SaveKind kind) {
	return kind == SaveKind::VR ? "vr" : "flat";
}

std::string vrMarkData() {
	return "Arx Fatalis VR: this save was made in VR and loads only in VR\n";
}

SaveKind saveKindOfMark(std::string_view markData) {
	return markData.empty() ? SaveKind::Flat : SaveKind::VR;
}

std::string saveKindRefusal(SaveKind save, SaveKind run) {

	if(save == run) {
		return std::string();
	}

	if(save == SaveKind::VR) {
		return "a VR save, and this run is flat - VR and flat games never load each other's saves (D-NEW)";
	}
	return "a flat save (no VR mark), and this run is in VR - VR and flat games never load each other's saves (D-NEW)";
}

bool writeSaveKind(SaveBlock & block, SaveKind kind) {

	if(kind == SaveKind::VR) {
		std::string mark = vrMarkData();
		return block.save(VRMarkFile, mark.data(), mark.size());
	}

	// A save block is copied from the save that was loaded: a flat save must not keep a mark it never earned
	block.remove(VRMarkFile);
	return true;
}

SaveKind readSaveKind(const fs::path & savefile) {
	return saveKindOfMark(SaveBlock::load(savefile, VRMarkFile));
}
