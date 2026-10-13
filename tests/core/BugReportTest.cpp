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

#include "core/BugReportTest.h"

#include <string>
#include <vector>

#include <boost/crc.hpp>

#include "core/BugReport.h"
#include "io/fs/Filesystem.h"

CppUnit::AutoRegisterSuite<BugReportTest> g_registerBugReportTest;

namespace {

const bugreport::PersonalNames Names = bugreport::usableNames({ "jdoe1" }, { "DESKTOP-K4Q2" });

std::string scrub(std::string_view text) {
	return bugreport::scrub(text, Names);
}

u32 get32(const std::string & data, size_t pos) {
	return u32(u8(data[pos])) | (u32(u8(data[pos + 1])) << 8) | (u32(u8(data[pos + 2])) << 16)
	       | (u32(u8(data[pos + 3])) << 24);
}

u16 get16(const std::string & data, size_t pos) {
	return u16(u8(data[pos]) | (u8(data[pos + 1]) << 8));
}

struct ReadEntry {
	std::string name;
	std::string data;
	u32 crc = 0;
};

//! Read a zip back through its central directory, checking every structure on the way
std::vector<ReadEntry> readZip(const std::string & zip) {

	std::vector<ReadEntry> result;

	CPPUNIT_ASSERT(zip.size() >= 22);
	size_t end = zip.size() - 22;
	CPPUNIT_ASSERT_EQUAL(u32(0x06054b50), get32(zip, end));
	u16 count = get16(zip, end + 10);
	size_t directory = get32(zip, end + 16);
	CPPUNIT_ASSERT_EQUAL(size_t(get32(zip, end + 12)), end - directory);

	size_t pos = directory;
	for(u16 i = 0; i < count; i++) {
		CPPUNIT_ASSERT_EQUAL(u32(0x02014b50), get32(zip, pos));
		CPPUNIT_ASSERT_EQUAL(u16(0), get16(zip, pos + 10)); // stored
		ReadEntry entry;
		entry.crc = get32(zip, pos + 16);
		u32 size = get32(zip, pos + 20);
		u16 nameLength = get16(zip, pos + 28);
		size_t local = get32(zip, pos + 42);
		entry.name = zip.substr(pos + 46, nameLength);
		pos += 46 + nameLength;

		CPPUNIT_ASSERT_EQUAL(u32(0x04034b50), get32(zip, local));
		CPPUNIT_ASSERT_EQUAL(entry.crc, get32(zip, local + 14));
		CPPUNIT_ASSERT_EQUAL(entry.name, zip.substr(local + 30, get16(zip, local + 26)));
		entry.data = zip.substr(local + 30 + get16(zip, local + 26) + get16(zip, local + 28), size);
		result.push_back(entry);
	}
	CPPUNIT_ASSERT_EQUAL(end, pos);

	return result;
}

} // anonymous namespace

void BugReportTest::setUp() {
	m_root = fs::current_path() / "bug-report-test-tmp";
	fs::remove_all(m_root);
	CPPUNIT_ASSERT(fs::create_directories(m_root));
}

void BugReportTest::tearDown() {
	fs::remove_all(m_root);
}

void BugReportTest::homePathTest() {
	CPPUNIT_ASSERT_EQUAL(std::string("Log: \"<home>\\Saved Games\\Arx Fatalis VR\\arx.log\""),
	                     scrub("Log: \"C:\\Users\\Some One\\Saved Games\\Arx Fatalis VR\\arx.log\""));
	CPPUNIT_ASSERT_EQUAL(std::string("<home>/AppData/Local"), scrub("c:/users/someone/AppData/Local"));
	CPPUNIT_ASSERT_EQUAL(std::string("at <home>\\x"), scrub("at D:\\Documents and Settings\\bob\\x"));
	CPPUNIT_ASSERT_EQUAL(std::string("<home>/.local/share/arx"), scrub("/home/alice/.local/share/arx"));
	CPPUNIT_ASSERT_EQUAL(std::string("<home>/Library"), scrub("/Users/alice/Library"));
	// Not a home folder: a word ending in "home"
	CPPUNIT_ASSERT_EQUAL(std::string("nohome/abc"), scrub("nohome/abc"));
}

void BugReportTest::steamIdTest() {
	CPPUNIT_ASSERT_EQUAL(std::string("D:\\Steam\\userdata\\<steam-id>\\760"), scrub("D:\\Steam\\userdata\\100985359\\760"));
	CPPUNIT_ASSERT_EQUAL(std::string("id <steam-id>"), scrub("id 76561198061251087"));
	CPPUNIT_ASSERT_EQUAL(std::string("<steam-id> <steam-id>"), scrub("STEAM_0:1:2345 [U:1:4690]"));
}

void BugReportTest::otherIdentifiersTest() {
	CPPUNIT_ASSERT_EQUAL(std::string("on <path>"), scrub("on \\\\server\\share"));
	CPPUNIT_ASSERT_EQUAL(std::string("mail <email>"), scrub("mail someone@example.com"));
	CPPUNIT_ASSERT_EQUAL(std::string("device <guid>"), scrub("device 0fa3c2d4-1b2c-4d5e-8f90-1234567890ab"));
	CPPUNIT_ASSERT_EQUAL(std::string("<sid>"), scrub("S-1-5-21-1004336348-1177238915-682003330-512"));
	CPPUNIT_ASSERT_EQUAL(std::string("mac <mac>"), scrub("mac 00:1a:2b:3c:4d:5e"));
	CPPUNIT_ASSERT_EQUAL(std::string("host <ip> up"), scrub("host 192.168.1.20 up"));
	// A four-part version after a version word, and the loopback, stay
	CPPUNIT_ASSERT_EQUAL(std::string("driver: 31.0.15.46, 127.0.0.1"), scrub("driver: 31.0.15.46, 127.0.0.1"));
}

void BugReportTest::namesTest() {
	CPPUNIT_ASSERT_EQUAL(std::string("account <user> on <machine>"), scrub("account jdoe1 on DESKTOP-K4Q2"));
	CPPUNIT_ASSERT_EQUAL(std::string("account <user>"), scrub("account JDOE1"));
	// Not inside a longer word or a hyphenated name: the GitHub login that starts with the account name stays
	CPPUNIT_ASSERT_EQUAL(std::string("jdoe1-dev jdoe12"), scrub("jdoe1-dev jdoe12"));
}

void BugReportTest::technicalTextStaysTest() {
	// What a bug needs: versions, the game data folder, file:line, a crash folder's date
	std::string text = "[I] Startup.cpp:247 Build: abc123 on main, Release; Steam data: "
	                   "\"D:\\games\\SteamLibrary\\steamapps\\common\\Arx Fatalis\"; 2026-10-09-18-50-15-00; v1.2.3.4";
	CPPUNIT_ASSERT_EQUAL(text, scrub(text));
}

void BugReportTest::usableNamesTest() {
	bugreport::PersonalNames names = bugreport::usableNames({ "Admin", "bob", "carol$", "Carol" }, { "WORKGROUP", "" });
	CPPUNIT_ASSERT_EQUAL(size_t(1), names.users.size());
	CPPUNIT_ASSERT_EQUAL(std::string("carol"), names.users[0]);
	CPPUNIT_ASSERT(names.machines.empty());
}

void BugReportTest::zipTest() {

	std::string binary("\x00\x01\x02\xff\r\n", 6);
	std::vector<bugreport::ZipEntry> entries = {
		{ "save0003/gsave.sav", binary },
		{ "arx.log", "a log\n" },
		{ "empty.txt", std::string() },
	};
	std::string zip = bugreport::makeZip(entries, 1760000000);

	std::vector<ReadEntry> read = readZip(zip);
	CPPUNIT_ASSERT_EQUAL(entries.size(), read.size());
	for(size_t i = 0; i < entries.size(); i++) {
		CPPUNIT_ASSERT_EQUAL(entries[i].name, read[i].name);
		CPPUNIT_ASSERT_EQUAL(entries[i].data, read[i].data);
		boost::crc_32_type crc;
		crc.process_bytes(entries[i].data.data(), entries[i].data.size());
		CPPUNIT_ASSERT_EQUAL(u32(crc.checksum()), read[i].crc);
	}

	CPPUNIT_ASSERT(readZip(bugreport::makeZip({ }, 1760000000)).empty());
}

void BugReportTest::fileNameTest() {
	std::string name = bugreport::reportFileName(1760000000);
	CPPUNIT_ASSERT_EQUAL(size_t(32), name.size());
	CPPUNIT_ASSERT_EQUAL(std::string("arxvr-report-"), name.substr(0, 13));
	CPPUNIT_ASSERT_EQUAL(std::string(".zip"), name.substr(28));
}

void BugReportTest::exportTest() {

	fs::path user = m_root / "user";
	fs::path save = user / "save" / "save0002" / "gsave.sav";
	CPPUNIT_ASSERT(fs::create_directories(save.parent()));
	std::string saveData = std::string("SAVE\0\1", 6) + " C:\\Users\\jdoe1 binary";
	CPPUNIT_ASSERT(fs::write(save, saveData));
	CPPUNIT_ASSERT(fs::write(user / "arx.log", "Log: \"C:\\Users\\jdoe1\\Saved Games\\Arx Fatalis VR\\arx.log\"\n"));
	CPPUNIT_ASSERT(fs::write(user / "arx-perf.log", "perf for jdoe1\n"));
	CPPUNIT_ASSERT(fs::write(user / "cfg.ini", "[misc]\nmachine=DESKTOP-K4Q2\n"));

	fs::path target = m_root / "desktop";
	bugreport::ExportResult result = bugreport::exportReport(save, user, user, target, 1760000000, Names);
	CPPUNIT_ASSERT(result.ok);
	CPPUNIT_ASSERT(result.why.empty());
	CPPUNIT_ASSERT_EQUAL(target / bugreport::reportFileName(1760000000), result.file);

	std::vector<ReadEntry> read = readZip(fs::read(result.file));
	CPPUNIT_ASSERT_EQUAL(size_t(4), read.size());
	CPPUNIT_ASSERT_EQUAL(std::string("save0002/gsave.sav"), read[0].name);
	// The save is binary and goes in untouched
	CPPUNIT_ASSERT_EQUAL(saveData, read[0].data);
	CPPUNIT_ASSERT_EQUAL(std::string("arx.log"), read[1].name);
	CPPUNIT_ASSERT_EQUAL(std::string("Log: \"<home>\\Saved Games\\Arx Fatalis VR\\arx.log\"\n"), read[1].data);
	CPPUNIT_ASSERT_EQUAL(std::string("arx-perf.log"), read[2].name);
	CPPUNIT_ASSERT_EQUAL(std::string("perf for <user>\n"), read[2].data);
	CPPUNIT_ASSERT_EQUAL(std::string("cfg.ini"), read[3].name);
	CPPUNIT_ASSERT_EQUAL(std::string("[misc]\nmachine=<machine>\n"), read[3].data);
}

void BugReportTest::exportWithoutLogsTest() {
	fs::path save = m_root / "save0000" / "gsave.sav";
	CPPUNIT_ASSERT(fs::create_directories(save.parent()));
	CPPUNIT_ASSERT(fs::write(save, "SAVE"));
	bugreport::ExportResult result = bugreport::exportReport(save, m_root, m_root, m_root / "out", 1760000000, Names);
	CPPUNIT_ASSERT(result.ok);
	CPPUNIT_ASSERT_EQUAL(size_t(1), result.files.size());
	CPPUNIT_ASSERT_EQUAL(std::string("not found, left out: arx.log arx-perf.log cfg.ini"), result.why);
}

void BugReportTest::exportWithoutSaveTest() {
	bugreport::ExportResult result = bugreport::exportReport(m_root / "none" / "gsave.sav", m_root, m_root,
	                                                         m_root / "out", 1760000000, Names);
	CPPUNIT_ASSERT(!result.ok);
	CPPUNIT_ASSERT(!fs::exists(m_root / "out"));
}
