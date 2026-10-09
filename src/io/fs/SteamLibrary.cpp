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

#include "io/fs/SteamLibrary.h"

#include <algorithm>
#include <sstream>

#include "io/fs/Filesystem.h"

#include "platform/Platform.h"

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
#include "platform/WindowsUtils.h"
#endif

#include "util/String.h"

namespace fs::steam {

namespace {

bool equalsIgnoreCase(std::string_view a, std::string_view b) {
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return util::toLowercase(x) == util::toLowercase(y);
	});
}

bool isNumber(std::string_view str) {
	return !str.empty() && std::all_of(str.begin(), str.end(), [](char c) { return c >= '0' && c <= '9'; });
}

class KeyValuesParser {

	std::string_view m_text;
	size_t m_pos = 0;

	enum TokenType { End, String, Open, Close, Error };

	void skipSpaceAndComments() {
		while(m_pos < m_text.size()) {
			char c = m_text[m_pos];
			if(c == ' ' || c == '\t' || c == '\r' || c == '\n') {
				m_pos++;
			} else if(c == '/' && m_pos + 1 < m_text.size() && m_text[m_pos + 1] == '/') {
				size_t eol = m_text.find('\n', m_pos);
				m_pos = (eol == std::string_view::npos) ? m_text.size() : eol;
			} else {
				break;
			}
		}
	}

	TokenType next(std::string & token) {

		skipSpaceAndComments();
		if(m_pos >= m_text.size()) {
			return End;
		}

		char c = m_text[m_pos];
		if(c == '{') {
			m_pos++;
			return Open;
		}
		if(c == '}') {
			m_pos++;
			return Close;
		}

		token.clear();

		if(c == '"') {
			m_pos++;
			while(m_pos < m_text.size()) {
				c = m_text[m_pos++];
				if(c == '"') {
					return String;
				}
				if(c == '\\' && m_pos < m_text.size()) {
					char escaped = m_text[m_pos++];
					switch(escaped) {
						case 'n': token += '\n'; break;
						case 't': token += '\t'; break;
						case '\\':
						case '"': token += escaped; break;
						default: token += '\\'; token += escaped; break;
					}
					continue;
				}
				token += c;
			}
			return Error; // unterminated string
		}

		// A bare token, e.g. a key written without quotes
		while(m_pos < m_text.size()) {
			c = m_text[m_pos];
			if(c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '"' || c == '{' || c == '}') {
				break;
			}
			token += c;
			m_pos++;
		}
		return String;
	}

	// Parse entries until the closing brace (nested) or the end of the text (top level)
	bool parseEntries(std::vector<KeyValue> & entries, bool nested) {

		std::string token;
		while(true) {

			TokenType type = next(token);
			if(type == End) {
				return !nested;
			}
			if(type == Close) {
				return nested;
			}
			if(type != String) {
				return false;
			}

			KeyValue entry;
			entry.key = token;

			type = next(token);
			if(type == String) {
				entry.value = token;
			} else if(type == Open) {
				entry.block = true;
				if(!parseEntries(entry.children, true)) {
					return false;
				}
			} else {
				return false;
			}

			// An optional platform conditional like [$WIN32] after the value
			skipSpaceAndComments();
			if(m_pos < m_text.size() && m_text[m_pos] == '[') {
				size_t end = m_text.find(']', m_pos);
				if(end == std::string_view::npos) {
					return false;
				}
				m_pos = end + 1;
			}

			entries.push_back(std::move(entry));
		}
	}

public:

	explicit KeyValuesParser(std::string_view text) : m_text(text) { }

	std::optional<std::vector<KeyValue>> parse() {
		std::vector<KeyValue> entries;
		if(!parseEntries(entries, false)) {
			return std::nullopt;
		}
		return entries;
	}

};

const KeyValue * findTop(const std::vector<KeyValue> & entries, std::string_view name) {
	for(const KeyValue & entry : entries) {
		if(equalsIgnoreCase(entry.key, name)) {
			return &entry;
		}
	}
	return nullptr;
}

void addRoot(std::vector<path> & roots, std::vector<std::string> & seen, std::string_view value) {
	if(value.empty()) {
		return;
	}
	path dir = path(value);
	// Steam writes "c:/program files (x86)/steam" in HKCU and "C:\Program Files (x86)\Steam" in HKLM
	std::string key = util::toLowercase(dir.string());
	if(std::find(seen.begin(), seen.end(), key) != seen.end()) {
		return;
	}
	seen.push_back(key);
	roots.push_back(dir);
}

} // anonymous namespace

const KeyValue * KeyValue::find(std::string_view name) const {
	return findTop(children, name);
}

std::optional<std::vector<KeyValue>> parseKeyValues(std::string_view text) {
	return KeyValuesParser(text).parse();
}

std::vector<std::string> parseLibraryFolders(std::string_view vdf) {

	std::vector<std::string> result;

	auto entries = parseKeyValues(vdf);
	if(!entries) {
		return result;
	}

	const KeyValue * root = findTop(*entries, "libraryfolders");
	if(!root || !root->block) {
		return result;
	}

	for(const KeyValue & library : root->children) {
		if(!isNumber(library.key)) {
			continue;
		}
		if(!library.block) {
			if(!library.value.empty()) {
				result.push_back(library.value); // pre-2021 format: "1" "D:\\SteamLibrary"
			}
		} else if(const KeyValue * folder = library.find("path"); folder && !folder->value.empty()) {
			result.push_back(folder->value);
		}
	}

	return result;
}

std::optional<std::string> parseAppManifestInstallDir(std::string_view acf) {

	auto entries = parseKeyValues(acf);
	if(!entries) {
		return std::nullopt;
	}

	const KeyValue * root = findTop(*entries, "AppState");
	if(!root) {
		return std::nullopt;
	}

	const KeyValue * installdir = root->find("installdir");
	if(!installdir || installdir->block || installdir->value.empty()) {
		return std::nullopt;
	}

	return installdir->value;
}

std::vector<path> getSteamRoots() {

	std::vector<path> roots;

#if ARX_PLATFORM == ARX_PLATFORM_WIN32

	#if defined(_WIN64)
	REGSAM foreign_registry = KEY_WOW64_32KEY;
	#else
	REGSAM foreign_registry = KEY_WOW64_64KEY;
	#endif

	const WCHAR * key = L"Software\\Valve\\Steam";
	std::vector<std::string> seen;

	for(REGSAM view : { REGSAM(0), foreign_registry }) {
		if(auto value = platform::getRegistryValue(HKEY_CURRENT_USER, key, L"SteamPath", view)) {
			addRoot(roots, seen, *value);
		}
	}
	for(REGSAM view : { REGSAM(0), foreign_registry }) {
		if(auto value = platform::getRegistryValue(HKEY_LOCAL_MACHINE, key, L"InstallPath", view)) {
			addRoot(roots, seen, *value);
		}
	}

#endif

	return roots;
}

AppInstall findAppInstall(unsigned appId, const std::vector<path> & steamRoots) {

	AppInstall result;

	if(steamRoots.empty()) {
		result.why = "no Steam install in the registry (HKCU/HKLM Software\\Valve\\Steam)";
		return result;
	}

	// Every Steam install is a library itself; libraryfolders.vdf lists the others
	std::vector<path> libraries;
	std::vector<std::string> seen;
	for(const path & root : steamRoots) {
		addRoot(libraries, seen, root.string());
		for(const char * folder : { "steamapps", "config" }) {
			path vdf = root / folder / "libraryfolders.vdf";
			if(is_regular_file(vdf)) {
				for(const std::string & library : parseLibraryFolders(read(vdf))) {
					addRoot(libraries, seen, library);
				}
				break;
			}
		}
	}

	std::ostringstream problems;
	std::string manifestName = "appmanifest_" + std::to_string(appId) + ".acf";
	for(const path & library : libraries) {

		path manifest = library / "steamapps" / manifestName;
		if(!is_regular_file(manifest)) {
			continue;
		}

		auto installdir = parseAppManifestInstallDir(read(manifest));
		if(!installdir) {
			problems << "; " << manifest << " has no \"installdir\"";
			continue;
		}

		path dir = library / "steamapps" / "common" / *installdir;
		if(!is_directory(dir)) {
			problems << "; " << manifest << " names " << dir << ", which does not exist";
			continue;
		}

		result.dir = dir;
		result.why = "Steam library " + library.string();
		return result;
	}

	std::ostringstream why;
	why << "app " << appId << " is not installed in any of " << libraries.size() << " Steam libraries (";
	for(size_t i = 0; i < libraries.size(); i++) {
		why << (i == 0 ? "" : ", ") << libraries[i];
	}
	why << ')' << problems.str();
	result.why = why.str();
	return result;
}

} // namespace fs::steam
