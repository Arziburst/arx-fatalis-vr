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

#ifndef ARX_IO_FS_STEAMLIBRARY_H
#define ARX_IO_FS_STEAMLIBRARY_H

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "io/fs/FilePath.h"

/*!
 * Finding an installed Steam app (Arx Fatalis is app 1700) without any configuration (D-004):
 * Steam folder from the registry -> steamapps/libraryfolders.vdf -> every library's
 * steamapps/appmanifest_<id>.acf -> "installdir" -> steamapps/common/<installdir>.
 *
 * The parsers are pure (text in, values out) and covered by tests/io/fs/SteamLibraryTest.cpp.
 */
namespace fs::steam {

//! The Steam app id of Arx Fatalis
inline constexpr unsigned ArxFatalisAppId = 1700;

/*!
 * One entry of a Valve KeyValues (VDF / ACF) text file: a quoted key followed by either a quoted
 * value or a { } block of entries.
 */
struct KeyValue {

	std::string key;
	std::string value;
	std::vector<KeyValue> children;
	bool block = false;

	//! The first child with this key (compared without case, as Steam does), or nullptr
	[[nodiscard]] const KeyValue * find(std::string_view name) const;

};

/*!
 * Parse Valve KeyValues text: quoted or bare tokens, \c { } blocks, \c // comments and the
 * escapes \c \\\\ \c \\" \c \\n \c \\t inside quotes.
 *
 * \return the top-level entries, or std::nullopt if the text is malformed (unbalanced braces,
 *         an unterminated string, a key without a value)
 */
[[nodiscard]] std::optional<std::vector<KeyValue>> parseKeyValues(std::string_view text);

/*!
 * Library folders listed in a libraryfolders.vdf, in file order.
 *
 * Handles the current format (\c "0" { "path" "C:\\..." }) and the pre-2021 one (\c "1" "D:\\...").
 * Only numbered entries count: the old format also has keys like "TimeNextStatsReport".
 */
[[nodiscard]] std::vector<std::string> parseLibraryFolders(std::string_view vdf);

//! The "installdir" of an appmanifest_<id>.acf, or std::nullopt if it has none
[[nodiscard]] std::optional<std::string> parseAppManifestInstallDir(std::string_view acf);

/*!
 * Steam install folders known to the system, most trusted first and without duplicates:
 * HKCU\\Software\\Valve\\Steam "SteamPath", then HKLM\\Software\\Valve\\Steam "InstallPath",
 * each in both registry views. Empty on other platforms.
 */
[[nodiscard]] std::vector<path> getSteamRoots();

struct AppInstall {

	//! The app's install folder, empty if it was not found
	path dir;

	//! The Steam library the app was found in, or why the search failed
	std::string why;

};

//! Find a Steam app's install folder in all Steam libraries of the given Steam installs
[[nodiscard]] AppInstall findAppInstall(unsigned appId, const std::vector<path> & steamRoots);

} // namespace fs::steam

#endif // ARX_IO_FS_STEAMLIBRARY_H
