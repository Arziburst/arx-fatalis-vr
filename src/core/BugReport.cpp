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

#include "core/BugReport.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iomanip>
#include <regex>
#include <sstream>

#include <boost/crc.hpp>

#include "io/fs/Filesystem.h"
#include "platform/Platform.h"
#include "util/String.h"

namespace bugreport {

namespace {

bool isWordChar(char c) {
	return std::isalnum(static_cast<unsigned char>(c)) || c == '_';
}

/*!
 * One scrub rule: what matches \c rx becomes \c put, unless \c keep says the text before the match forbids it (the
 * lookbehinds of the PowerShell rules, which std::regex does not have) - it gets the text and the match's start.
 * With \c keepPrefix the first group of the match stays and only the rest is replaced.
 */
struct Rule {
	std::regex rx;
	std::string put;
	std::function<bool(const std::string & text, size_t pos)> keep;
	bool keepPrefix = false;
};

std::string applyRule(const std::string & text, const Rule & rule) {
	
	std::string result;
	size_t last = 0;
	for(auto it = std::sregex_iterator(text.begin(), text.end(), rule.rx); it != std::sregex_iterator(); ++it) {
		const std::smatch & match = *it;
		size_t pos = size_t(match.position(0));
		if(rule.keep && rule.keep(text, pos)) {
			continue;
		}
		result.append(text, last, pos - last);
		if(rule.keepPrefix) {
			result += match.str(1);
		}
		result += rule.put;
		last = pos + size_t(match.length(0));
	}
	result.append(text, last, std::string::npos);
	
	return result;
}

std::string escapeRegex(std::string_view text) {
	std::string result;
	for(char c : text) {
		if(std::strchr("\\^$.|?*+()[]{}", c)) {
			result += '\\';
		}
		result += c;
	}
	return result;
}

//! A rule's lookbehind: the character before \a pos is one of \a chars, or a word character when \a word
std::function<bool(const std::string &, size_t)> after(const char * chars, bool word = true) {
	return [chars, word](const std::string & text, size_t pos) {
		return pos > 0 && ((word && isWordChar(text[pos - 1])) || std::strchr(chars, text[pos - 1]) != nullptr);
	};
}

//! An IPv4 address is kept after a version word: "version 1.2.3.4", "driver: 31.0.15.4601" (a four-part version)
bool afterVersionWord(const std::string & text, size_t pos) {
	if(pos > 0 && (isWordChar(text[pos - 1]) || text[pos - 1] == '.')) {
		return true;
	}
	size_t end = pos;
	for(size_t i = 0; i < 3 && end > 0 && std::strchr(" \t:=", text[end - 1]); i++) {
		end--;
	}
	size_t begin = end;
	while(begin > 0 && isWordChar(text[begin - 1])) {
		begin--;
	}
	std::string word = util::toLowercase(std::string_view(text).substr(begin, end - begin));
	return word == "version" || word == "ver" || word == "v" || word == "build" || word == "driver";
}

const std::vector<Rule> & fixedRules() {
	
	constexpr auto icase = std::regex::ECMAScript | std::regex::icase;
	
	// The order and the patterns of $script:ArxScrubRules (log-digest-lib.ps1); a Windows user name cannot
	// contain " / \ [ ] : ; | = , + * ? < >
	static const std::vector<Rule> rules = {
		{ std::regex(R"([A-Za-z]:[\\/]+(?:Users|Documents and Settings)[\\/]+(?:<user>|[^\\/"'`\r\n\[\]:;|=,+*?<>]+))", icase),
		  "<home>", after(""), false },
		{ std::regex(R"(/(?:Users|home)/(?:<user>|[^/\s"'`<>]+))"), "<home>", after(".~"), false },
		{ std::regex(R"((userdata[\\/]+)\d+)", icase), "<steam-id>", nullptr, true },
		{ std::regex(R"(7656119\d{10}\b|STEAM_[0-5]:[01]:\d+\b|\[U:1:\d+\])"), "<steam-id>", after(""), false },
		{ std::regex(R"(\\\\(?![?.]\\)[^\\\s"'`]+\\[^\\\s"'`]+)"), "<path>", after("\\:"), false },
		{ std::regex(R"([a-z0-9._%+-]+@[a-z0-9-]+(?:\.[a-z0-9-]+)*\.[a-z]{2,}\b)", icase), "<email>", after(""), false },
		{ std::regex(R"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}\b)", icase), "<guid>", after(""), false },
		{ std::regex(R"(S-1-5-21(?:-\d+){1,4}\b)"), "<sid>", after(""), false },
		// Not a crash folder's date: 2026-10-09-18-50-15-00
		{ std::regex(R"((?:[0-9a-f]{2}[:-]){5}[0-9a-f]{2}(?![\w:-]))", icase), "<mac>", after(":-"), false },
		{ std::regex(R"((?!127\.)(?:(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)\.){3}(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(?![\w.]))"),
		  "<ip>", afterVersionWord, false },
	};
	
	return rules;
}
void addNames(std::vector<std::string> & out, const std::vector<std::string> & names) {
	
	static const char * const generic[] = {
		"SYSTEM", "Administrator", "Admin", "User", "Public", "Default", "Guest", "LOCAL SERVICE", "NETWORK SERVICE",
		"WORKGROUP"
	};
	
	for(std::string name : names) {
		while(!name.empty() && name.back() == '$') {
			name.pop_back();
		}
		bool isGeneric = std::any_of(std::begin(generic), std::end(generic), [&](const char * g) {
			return util::toLowercase(name) == util::toLowercase(g);
		});
		bool known = std::any_of(out.begin(), out.end(), [&](const std::string & n) {
			return util::toLowercase(n) == util::toLowercase(name);
		});
		if(name.size() >= 4 && !isGeneric && !known) {
			out.push_back(name);
		}
	}
	
}

std::string environmentValue(const char * name) {
	const char * value = std::getenv(name);
	return value ? std::string(value) : std::string();
}

void put16(std::string & out, u16 value) {
	out += char(value & 0xff);
	out += char(value >> 8);
}

void put32(std::string & out, u32 value) {
	put16(out, u16(value & 0xffff));
	put16(out, u16(value >> 16));
}

} // anonymous namespace

PersonalNames usableNames(const std::vector<std::string> & users, const std::vector<std::string> & machines) {
	PersonalNames result;
	addNames(result.users, users);
	addNames(result.machines, machines);
	return result;
}

PersonalNames systemNames() {
	return usableNames({ environmentValue("USERNAME") },
	                   { environmentValue("COMPUTERNAME"), environmentValue("USERDOMAIN") });
}

std::string scrub(std::string_view input, const PersonalNames & names) {
	
	std::string text(input);
	
	for(const Rule & rule : fixedRules()) {
		text = applyRule(text, rule);
	}
	
	// Where they stand alone: not inside a longer word or a hyphenated name (the GitHub login stays)
	auto nameRule = [](const std::string & name, const char * put) {
		return Rule{ std::regex(escapeRegex(name) + R"((?![\w-]))", std::regex::ECMAScript | std::regex::icase),
		             put, after("-"), false };
	};
	for(const std::string & name : names.users) {
		text = applyRule(text, nameRule(name, "<user>"));
	}
	for(const std::string & name : names.machines) {
		text = applyRule(text, nameRule(name, "<machine>"));
	}
	
	return text;
}

std::string makeZip(const std::vector<ZipEntry> & entries, std::time_t time) {
	
	// MS-DOS date and time of the entries
	std::tm local = *std::localtime(&time);
	u16 dosTime = u16((local.tm_hour << 11) | (local.tm_min << 5) | (local.tm_sec / 2));
	u16 dosDate = u16(((std::max(local.tm_year, 80) - 80) << 9) | ((local.tm_mon + 1) << 5) | local.tm_mday);
	
	std::string zip;
	std::string directory;
	
	for(const ZipEntry & entry : entries) {
		
		boost::crc_32_type crc;
		crc.process_bytes(entry.data.data(), entry.data.size());
		u32 checksum = crc.checksum();
		u32 offset = u32(zip.size());
		
		// Local file header, stored (method 0), UTF-8 names (flag bit 11)
		put32(zip, 0x04034b50);
		put16(zip, 20);
		put16(zip, 0x0800);
		put16(zip, 0);
		put16(zip, dosTime);
		put16(zip, dosDate);
		put32(zip, checksum);
		put32(zip, u32(entry.data.size()));
		put32(zip, u32(entry.data.size()));
		put16(zip, u16(entry.name.size()));
		put16(zip, 0);
		zip += entry.name;
		zip += entry.data;
		
		// Central directory record
		put32(directory, 0x02014b50);
		put16(directory, 20);
		put16(directory, 20);
		put16(directory, 0x0800);
		put16(directory, 0);
		put16(directory, dosTime);
		put16(directory, dosDate);
		put32(directory, checksum);
		put32(directory, u32(entry.data.size()));
		put32(directory, u32(entry.data.size()));
		put16(directory, u16(entry.name.size()));
		put16(directory, 0);
		put16(directory, 0);
		put16(directory, 0);
		put16(directory, 0);
		put32(directory, 0);
		put32(directory, offset);
		directory += entry.name;
		
	}
	
	u32 directoryOffset = u32(zip.size());
	zip += directory;
	
	// End of central directory
	put32(zip, 0x06054b50);
	put16(zip, 0);
	put16(zip, 0);
	put16(zip, u16(entries.size()));
	put16(zip, u16(entries.size()));
	put32(zip, u32(directory.size()));
	put32(zip, directoryOffset);
	put16(zip, 0);
	
	return zip;
}

std::string reportFileName(std::time_t time) {
	std::tm local = *std::localtime(&time);
	std::ostringstream oss;
	oss << "arxvr-report-" << std::put_time(&local, "%Y%m%d-%H%M%S") << ".zip";
	return oss.str();
}

ExportResult exportReport(const fs::path & savefile, const fs::path & userDir, const fs::path & configDir,
                          const fs::path & targetDir, std::time_t time, const PersonalNames & names) {
	
	ExportResult result;
	
	if(!fs::is_regular_file(savefile)) {
		result.why = "no save file " + savefile.string();
		return result;
	}
	
	std::vector<ZipEntry> entries;
	std::string slot(savefile.parent().filename());
	entries.push_back(ZipEntry{ slot + "/" + std::string(savefile.filename()), fs::read(savefile) });
	
	std::vector<std::string> missing;
	const fs::path texts[] = { userDir / "arx.log", userDir / "arx-perf.log", configDir / "cfg.ini" };
	for(const fs::path & file : texts) {
		if(fs::is_regular_file(file)) {
			entries.push_back(ZipEntry{ std::string(file.filename()), scrub(fs::read(file), names) });
		} else {
			missing.push_back(std::string(file.filename()));
		}
	}
	
	if(!fs::create_directories(targetDir)) {
		result.why = "could not create " + targetDir.string();
		return result;
	}
	
	result.file = targetDir / reportFileName(time);
	if(!fs::write(result.file, makeZip(entries, time))) {
		result.why = "could not write " + result.file.string();
		return result;
	}
	
	result.ok = true;
	for(const ZipEntry & entry : entries) {
		result.files.push_back(entry.name);
	}
	if(!missing.empty()) {
		result.why = "not found, left out:";
		for(const std::string & name : missing) {
			result.why += " " + name;
		}
	}
	
	return result;
}

} // namespace bugreport
