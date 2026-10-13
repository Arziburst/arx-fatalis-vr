/*
 * Copyright 2012-2019 Arx Libertatis Team (see the AUTHORS file)
 *
 * This file is part of Arx Libertatis.
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

#ifndef ARX_IO_FS_SYSTEMPATHS_H
#define ARX_IO_FS_SYSTEMPATHS_H

#include <string>
#include <vector>
#include <ostream>

#include "io/fs/FilePath.h"
#include "util/cmdline/CommandLine.h"

namespace fs {

/*!
 * \brief Initialize the system resource paths
 *
 * Uses arguments provided on the command line, if any.
 */
ExitStatus initSystemPaths();

//! \return A directory for saves and user-specific data files
const path & getUserDir();

//! \return A directory for config files
const path & getConfigDir();

//! \return Directories for data files
const std::vector<path> & getDataDirs();

/*!
 * \return How the user directory was chosen: "command-line" (--user-dir), "registry", "default" or "fallback" -
 *         for the startup banner and status.json
 */
const char * getUserDirSource();

/*!
 * \return cfg.ini of the user directory earlier builds used (Arx Libertatis, #417) when this run found its user
 *         and config directories by the standard search (no --user-dir / --config-dir, no registry value) and that
 *         file exists; empty otherwise
 */
const path & getLegacyConfigFile();

//! What \ref copyLegacyConfig did
struct LegacyConfigCopy {
	bool copied = false;
	std::string why; //!< what was copied, or why nothing was
};

/*!
 * The first start's settings copy (#417, D-NEW): \a legacyConfigFile is copied to \a configFile when that does not
 * exist yet and \a userDir has no save folder yet (the game creates it on its first start), so it happens once.
 * Settings only - saves are never copied: a flat save must never reach the VR game.
 */
LegacyConfigCopy copyLegacyConfig(const path & configFile, const path & legacyConfigFile, const path & userDir);

/*!
 * \return What the lookup of Arx Fatalis in the Steam libraries found (the folder and the library),
 *         or why it found nothing — for the startup banner
 */
const std::string & getSteamDataStatus();

/*!
 * \brief Get a list of all data search paths
 *
 * These are all the paths that are considered for the list returned by \ref getDataDirs
 */
std::vector<path> getDataSearchPaths();

/*!
	* \brief Find a resource in the data search path
	*
	* If resource is an absolute path, return it directoly if it exists.
	*
	* If resource is a relative path, look for it in each data directory
	* (ordered by decreasing priority) and return the first full path that
	* exists.
	*
	* \param resource the relative resource name to look for.
	*
	* \return an empty path or an existing file.
	*/
path findDataFile(const path & resource);

} // namespace fs

#endif // ARX_IO_FS_SYSTEMPATHS_H
