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

#include "core/SaveSlotsTest.h"

#include <string>
#include <vector>

#include "core/SaveSlots.h"

CppUnit::AutoRegisterSuite<SaveSlotsTest> g_registerSaveSlotsTest;

void SaveSlotsTest::testReplaySlotsAreTheFiveNamed() {
	for(const char * slot : { "save9000", "save9001", "save9002", "save9003", "save9004" }) {
		CPPUNIT_ASSERT_MESSAGE(slot, isReplaySaveSlot(slot));
	}
	for(const char * folder : { "save0000", "save0001", "save8999", "save9005", "save9100", "save900", "save90000",
	                            "Save9000", "replay9000", "save9000x", "" }) {
		CPPUNIT_ASSERT_MESSAGE(folder, !isReplaySaveSlot(folder));
	}
}

void SaveSlotsTest::testReplaySlotsAreNeverListed() {

	// Only replay slots in the save directory (a replay's user directory before its save is copied in, a player's
	// first VR session before any save of their own): the list is empty - no slot 0, nothing to quickload
	const std::vector<std::string> none = listedSaveFolders({ "save9000", "save9003" });
	CPPUNIT_ASSERT(none.empty());

	// Among the player's saves they drop out, the rest keep their order
	const std::vector<std::string> listed = listedSaveFolders({ "save0001", "save9000", "save0000", "save9004", "save0002" });
	const std::vector<std::string> expected = { "save0001", "save0000", "save0002" };
	CPPUNIT_ASSERT(listed == expected);

	// An empty directory
	CPPUNIT_ASSERT(listedSaveFolders({ }).empty());
}
