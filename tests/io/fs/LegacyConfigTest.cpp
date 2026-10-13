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

#include "io/fs/LegacyConfigTest.h"

#include <string>

#include "io/fs/Filesystem.h"
#include "io/fs/SystemPaths.h"

CppUnit::AutoRegisterSuite<LegacyConfigTest> g_registerLegacyConfigTest;

namespace {

const std::string LegacySettings = "[video]\nfullscreen=false\n";

} // anonymous namespace

void LegacyConfigTest::setUp() {
	// Saved Games\Arx Libertatis with the player's settings and a save, and our own, still empty directory
	m_root = fs::current_path() / "legacy-config-test-tmp";
	fs::remove_all(m_root);
	m_legacy = m_root / "Arx Libertatis";
	m_user = m_root / "Arx Fatalis VR";
	CPPUNIT_ASSERT(fs::create_directories(m_legacy / "save" / "save0000"));
	CPPUNIT_ASSERT(fs::write(m_legacy / "cfg.ini", LegacySettings));
	CPPUNIT_ASSERT(fs::write(m_legacy / "save" / "save0000" / "gsave.sav", "a flat save"));
	CPPUNIT_ASSERT(fs::create_directories(m_user));
}

void LegacyConfigTest::tearDown() {
	fs::remove_all(m_root);
}

void LegacyConfigTest::firstStartTest() {
	fs::LegacyConfigCopy copy = fs::copyLegacyConfig(m_user / "cfg.ini", m_legacy / "cfg.ini", m_user);
	CPPUNIT_ASSERT(copy.copied);
	CPPUNIT_ASSERT_EQUAL(LegacySettings, fs::read(m_user / "cfg.ini"));
	// A copy, not a move: flat Arx Libertatis keeps its settings
	CPPUNIT_ASSERT_EQUAL(LegacySettings, fs::read(m_legacy / "cfg.ini"));
}

void LegacyConfigTest::onlyOnceTest() {
	CPPUNIT_ASSERT(fs::copyLegacyConfig(m_user / "cfg.ini", m_legacy / "cfg.ini", m_user).copied);
	// The game creates the save folder on its first start; a cfg.ini the player deleted later is not replaced
	CPPUNIT_ASSERT(fs::create_directories(m_user / "save"));
	CPPUNIT_ASSERT(fs::remove(m_user / "cfg.ini"));
	fs::LegacyConfigCopy again = fs::copyLegacyConfig(m_user / "cfg.ini", m_legacy / "cfg.ini", m_user);
	CPPUNIT_ASSERT(!again.copied);
	CPPUNIT_ASSERT_EQUAL(std::string("not the first start (the save folder exists)"), again.why);
	CPPUNIT_ASSERT(!fs::exists(m_user / "cfg.ini"));
}

void LegacyConfigTest::ownConfigKeptTest() {
	CPPUNIT_ASSERT(fs::write(m_user / "cfg.ini", "[misc]\n"));
	fs::LegacyConfigCopy copy = fs::copyLegacyConfig(m_user / "cfg.ini", m_legacy / "cfg.ini", m_user);
	CPPUNIT_ASSERT(!copy.copied);
	CPPUNIT_ASSERT_EQUAL(std::string("[misc]\n"), fs::read(m_user / "cfg.ini"));
}

void LegacyConfigTest::noLegacyTest() {
	fs::LegacyConfigCopy copy = fs::copyLegacyConfig(m_user / "cfg.ini", fs::path(), m_user);
	CPPUNIT_ASSERT(!copy.copied);
	CPPUNIT_ASSERT(!fs::exists(m_user / "cfg.ini"));
	copy = fs::copyLegacyConfig(m_user / "cfg.ini", m_root / "missing" / "cfg.ini", m_user);
	CPPUNIT_ASSERT(!copy.copied);
}

void LegacyConfigTest::savesNeverCopiedTest() {
	CPPUNIT_ASSERT(fs::copyLegacyConfig(m_user / "cfg.ini", m_legacy / "cfg.ini", m_user).copied);
	CPPUNIT_ASSERT(!fs::exists(m_user / "save"));
}
