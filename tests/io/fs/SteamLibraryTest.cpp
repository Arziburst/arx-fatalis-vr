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

#include "io/fs/SteamLibraryTest.h"

#include <string>
#include <vector>

#include "io/fs/Filesystem.h"
#include "io/fs/SteamLibrary.h"

CppUnit::AutoRegisterSuite<SteamLibraryTest> g_registerSteamLibraryTest;

namespace {

// A Windows path as Steam writes it into a VDF file: backslashes, each one escaped
std::string vdfPath(const fs::path & dir) {
	std::string result;
	for(char c : dir.string()) {
		result += (c == '/') ? std::string("\\\\") : std::string(1, c);
	}
	return result;
}

std::string manifest(std::string_view installdir) {
	return "\"AppState\"\n{\n\t\"appid\"\t\t\"1700\"\n\t\"name\"\t\t\"Arx Fatalis\"\n"
	       "\t\"installdir\"\t\t\"" + std::string(installdir) + "\"\n}\n";
}

} // anonymous namespace

void SteamLibraryTest::setUp() {
	m_root = fs::current_path() / "steam-library-test-tmp";
	fs::remove_all(m_root);
	CPPUNIT_ASSERT(fs::create_directories(m_root));
}

void SteamLibraryTest::tearDown() {
	fs::remove_all(m_root);
}

void SteamLibraryTest::keyValuesTest() {

	auto entries = fs::steam::parseKeyValues(
		"// a comment\n"
		"\"root\" {\n"
		"  bare \"value with \\\"quotes\\\" and \\\\ backslash\"\n"
		"  \"nested\" { \"a\" \"1\" } // trailing comment\n"
		"  \"cond\" \"x\" [$WIN32]\n"
		"}\n");
	CPPUNIT_ASSERT(entries.has_value());
	CPPUNIT_ASSERT_EQUAL(size_t(1), entries->size());

	const fs::steam::KeyValue & root = entries->front();
	CPPUNIT_ASSERT_EQUAL(std::string("root"), root.key);
	CPPUNIT_ASSERT(root.block);
	CPPUNIT_ASSERT_EQUAL(size_t(3), root.children.size());

	const fs::steam::KeyValue * bare = root.find("BARE"); // keys compare without case
	CPPUNIT_ASSERT(bare != nullptr);
	CPPUNIT_ASSERT_EQUAL(std::string("value with \"quotes\" and \\ backslash"), bare->value);

	const fs::steam::KeyValue * nested = root.find("nested");
	CPPUNIT_ASSERT(nested != nullptr && nested->block);
	CPPUNIT_ASSERT(nested->find("a") != nullptr);
	CPPUNIT_ASSERT_EQUAL(std::string("1"), nested->find("a")->value);

	CPPUNIT_ASSERT_EQUAL(std::string("x"), root.find("cond")->value);
	CPPUNIT_ASSERT(root.find("missing") == nullptr);
}

void SteamLibraryTest::malformedKeyValuesTest() {
	CPPUNIT_ASSERT(!fs::steam::parseKeyValues("\"root\" { \"a\" \"1\"").has_value()); // no closing brace
	CPPUNIT_ASSERT(!fs::steam::parseKeyValues("\"root\" { } }").has_value()); // extra closing brace
	CPPUNIT_ASSERT(!fs::steam::parseKeyValues("\"root\" \"unterminated").has_value());
	CPPUNIT_ASSERT(!fs::steam::parseKeyValues("\"key-without-value\"").has_value());
	CPPUNIT_ASSERT(fs::steam::parseKeyValues("").has_value());
	CPPUNIT_ASSERT(fs::steam::parseLibraryFolders("\"libraryfolders\" { \"0\" { \"path\"").empty());
	CPPUNIT_ASSERT(!fs::steam::parseAppManifestInstallDir("garbage {").has_value());
}

void SteamLibraryTest::libraryFoldersTest() {

	// The format of Steam since 2021, as on the owner's PC
	std::vector<std::string> libraries = fs::steam::parseLibraryFolders(
		"\"libraryfolders\"\n{\n"
		"\t\"0\"\n\t{\n"
		"\t\t\"path\"\t\t\"C:\\\\Program Files (x86)\\\\Steam\"\n"
		"\t\t\"label\"\t\t\"\"\n"
		"\t\t\"apps\"\n\t\t{\n\t\t\t\"228980\"\t\t\"509278569\"\n\t\t}\n"
		"\t}\n"
		"\t\"1\"\n\t{\n"
		"\t\t\"path\"\t\t\"D:\\\\games\\\\SteamLibrary\"\n"
		"\t\t\"apps\"\n\t\t{\n\t\t\t\"1700\"\t\t\"777298780\"\n\t\t}\n"
		"\t}\n"
		"}\n");

	CPPUNIT_ASSERT_EQUAL(size_t(2), libraries.size());
	CPPUNIT_ASSERT_EQUAL(std::string("C:\\Program Files (x86)\\Steam"), libraries[0]);
	CPPUNIT_ASSERT_EQUAL(std::string("D:\\games\\SteamLibrary"), libraries[1]);
}

void SteamLibraryTest::oldLibraryFoldersTest() {

	// The format before 2021: numbered string values, plus non-library keys
	std::vector<std::string> libraries = fs::steam::parseLibraryFolders(
		"\"LibraryFolders\"\n{\n"
		"\t\"TimeNextStatsReport\"\t\t\"1622000000\"\n"
		"\t\"ContentStatsID\"\t\t\"-123\"\n"
		"\t\"1\"\t\t\"E:\\\\SteamLibrary\"\n"
		"}\n");

	CPPUNIT_ASSERT_EQUAL(size_t(1), libraries.size());
	CPPUNIT_ASSERT_EQUAL(std::string("E:\\SteamLibrary"), libraries[0]);
}

void SteamLibraryTest::appManifestTest() {

	auto installdir = fs::steam::parseAppManifestInstallDir(manifest("Arx Fatalis"));
	CPPUNIT_ASSERT(installdir.has_value());
	CPPUNIT_ASSERT_EQUAL(std::string("Arx Fatalis"), *installdir);

	CPPUNIT_ASSERT(!fs::steam::parseAppManifestInstallDir("\"AppState\" { \"appid\" \"1700\" }").has_value());
	CPPUNIT_ASSERT(!fs::steam::parseAppManifestInstallDir(manifest("")).has_value());
	CPPUNIT_ASSERT(!fs::steam::parseAppManifestInstallDir("\"Other\" { \"installdir\" \"x\" }").has_value());
}

void SteamLibraryTest::findInSecondLibraryTest() {

	fs::path steam = m_root / "Steam";
	fs::path library = m_root / "SteamLibrary";
	fs::path game = library / "steamapps" / "common" / "Arx Fatalis";

	CPPUNIT_ASSERT(fs::create_directories(steam / "steamapps"));
	CPPUNIT_ASSERT(fs::create_directories(game));

	CPPUNIT_ASSERT(fs::write(steam / "steamapps" / "libraryfolders.vdf",
		"\"libraryfolders\"\n{\n"
		"\t\"0\" { \"path\" \"" + vdfPath(steam) + "\" }\n"
		"\t\"1\" { \"path\" \"" + vdfPath(library) + "\" }\n"
		"}\n"));

	// A stale manifest in the first library whose folder is gone must not stop the search
	CPPUNIT_ASSERT(fs::write(steam / "steamapps" / "appmanifest_1700.acf", manifest("Deleted Arx")));
	CPPUNIT_ASSERT(fs::write(library / "steamapps" / "appmanifest_1700.acf", manifest("Arx Fatalis")));

	fs::steam::AppInstall install = fs::steam::findAppInstall(fs::steam::ArxFatalisAppId, { steam });
	CPPUNIT_ASSERT_EQUAL_MESSAGE(install.why, game, install.dir);
	CPPUNIT_ASSERT_EQUAL(std::string("Steam library ") + library.string(), install.why);
}

void SteamLibraryTest::notFoundTest() {

	fs::steam::AppInstall none = fs::steam::findAppInstall(fs::steam::ArxFatalisAppId, { });
	CPPUNIT_ASSERT(none.dir.empty());
	CPPUNIT_ASSERT(none.why.find("no Steam install") != std::string::npos);

	fs::path steam = m_root / "Steam";
	CPPUNIT_ASSERT(fs::create_directories(steam / "steamapps"));
	CPPUNIT_ASSERT(fs::write(steam / "steamapps" / "appmanifest_1700.acf", manifest("Arx Fatalis")));

	fs::steam::AppInstall missing = fs::steam::findAppInstall(fs::steam::ArxFatalisAppId, { steam });
	CPPUNIT_ASSERT(missing.dir.empty());
	CPPUNIT_ASSERT_MESSAGE(missing.why, missing.why.find("not installed in any of 1 Steam libraries") != std::string::npos);
	CPPUNIT_ASSERT_MESSAGE(missing.why, missing.why.find("does not exist") != std::string::npos);
}
