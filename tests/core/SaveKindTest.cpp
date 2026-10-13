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

#include "core/SaveKindTest.h"

#include <string>

#include "core/SaveKind.h"
#include "io/SaveBlock.h"
#include "io/fs/Filesystem.h"

CppUnit::AutoRegisterSuite<SaveKindTest> g_registerSaveKindTest;

namespace {

const std::string PlayerData = "player level data stands in for pld";

//! A save block the way ARX_CHANGELEVEL_Save writes one: pld, then the kind, then the flush
bool writeSave(const fs::path & file, SaveKind kind) {
	SaveBlock block(file);
	if(!block.open(true)) {
		return false;
	}
	block.save("pld", PlayerData.data(), PlayerData.size());
	if(!writeSaveKind(block, kind)) {
		return false;
	}
	return block.flush("pld");
}

} // anonymous namespace

void SaveKindTest::setUp() {
	m_root = fs::current_path() / "save-kind-test-tmp";
	fs::remove_all(m_root);
	CPPUNIT_ASSERT(fs::create_directories(m_root));
}

void SaveKindTest::tearDown() {
	fs::remove_all(m_root);
}

void SaveKindTest::namesTest() {
	CPPUNIT_ASSERT_EQUAL(std::string("vr"), std::string(saveKindName(SaveKind::VR)));
	CPPUNIT_ASSERT_EQUAL(std::string("flat"), std::string(saveKindName(SaveKind::Flat)));
}

void SaveKindTest::markTest() {
	// No mark is a flat save - every save of flat Arx Libertatis and the original game
	CPPUNIT_ASSERT(saveKindOfMark(std::string()) == SaveKind::Flat);
	CPPUNIT_ASSERT(saveKindOfMark(vrMarkData()) == SaveKind::VR);
	// Any mark at all is a VR save: a later build may write more into it
	CPPUNIT_ASSERT(saveKindOfMark("x") == SaveKind::VR);
	CPPUNIT_ASSERT(!vrMarkData().empty());
}

void SaveKindTest::refusalTest() {
	CPPUNIT_ASSERT(saveKindRefusal(SaveKind::VR, SaveKind::VR).empty());
	CPPUNIT_ASSERT(saveKindRefusal(SaveKind::Flat, SaveKind::Flat).empty());
	std::string vrInFlat = saveKindRefusal(SaveKind::VR, SaveKind::Flat);
	std::string flatInVR = saveKindRefusal(SaveKind::Flat, SaveKind::VR);
	CPPUNIT_ASSERT(vrInFlat.find("a VR save") != std::string::npos);
	CPPUNIT_ASSERT(vrInFlat.find("this run is flat") != std::string::npos);
	CPPUNIT_ASSERT(flatInVR.find("a flat save") != std::string::npos);
	CPPUNIT_ASSERT(flatInVR.find("this run is in VR") != std::string::npos);
}

void SaveKindTest::vrSaveBlockTest() {
	fs::path file = m_root / "vr.sav";
	CPPUNIT_ASSERT(writeSave(file, SaveKind::VR));
	CPPUNIT_ASSERT(readSaveKind(file) == SaveKind::VR);
	CPPUNIT_ASSERT_EQUAL(vrMarkData(), SaveBlock::load(file, VRMarkFile));
	// The mark changes nothing else: pld reads as before
	CPPUNIT_ASSERT_EQUAL(PlayerData, SaveBlock::load(file, "pld"));
}

void SaveKindTest::flatSaveBlockTest() {
	fs::path file = m_root / "flat.sav";
	CPPUNIT_ASSERT(writeSave(file, SaveKind::Flat));
	CPPUNIT_ASSERT(readSaveKind(file) == SaveKind::Flat);
	CPPUNIT_ASSERT_EQUAL(PlayerData, SaveBlock::load(file, "pld"));
}

void SaveKindTest::flatSaveOfVRBlockTest() {
	// The game's current save block starts as a copy of the save that was loaded: a flat save written from a VR
	// block must lose the mark
	fs::path file = m_root / "current.sav";
	CPPUNIT_ASSERT(writeSave(file, SaveKind::VR));
	CPPUNIT_ASSERT(readSaveKind(file) == SaveKind::VR);
	CPPUNIT_ASSERT(writeSave(file, SaveKind::Flat));
	CPPUNIT_ASSERT(readSaveKind(file) == SaveKind::Flat);
	CPPUNIT_ASSERT(writeSave(file, SaveKind::VR));
	CPPUNIT_ASSERT(readSaveKind(file) == SaveKind::VR);
}

void SaveKindTest::missingFileTest() {
	// Nothing to read is no mark: the list skips such a save before it asks (GetInfo fails first)
	CPPUNIT_ASSERT(readSaveKind(m_root / "none.sav") == SaveKind::Flat);
}
