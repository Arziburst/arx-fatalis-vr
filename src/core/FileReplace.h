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

#ifndef ARX_CORE_FILEREPLACE_H
#define ARX_CORE_FILEREPLACE_H

#include <chrono>
#include <filesystem>
#include <system_error>
#include <thread>

/*!
 * \file
 * \brief Replace a file that a reader may hold open for a moment (the test channel's status.json, #23)
 *
 * The engine's \c fs::rename waits 100 ms per try for a file a reader holds: the test runner reads status.json all the
 * time, and the game's main thread stalled for 100-120 ms inside a frame. This replaces the file with short tries:
 *  - \ref ReplaceMode::Once: one try, for a write that can be skipped (the 250 ms timer's: the next one is near);
 *  - \ref ReplaceMode::Patient: tries every millisecond until the file is replaced or \a patience has run out. A write a
 *    test waits for must not be dropped: a reader holds the file for milliseconds, so this delays by that much, and
 *    the caller keeps the write pending when even the patience ran out.
 *
 * Header only, no engine globals: covered by tests/core/FileReplaceTest.cpp.
 */
namespace core {

enum class ReplaceMode {
	Once,
	Patient
};

/*!
 * \return true when \a to now holds \a from's content (\a from is gone); false with \a to unchanged
 */
inline bool replaceFile(const std::filesystem::path & from, const std::filesystem::path & to, ReplaceMode mode,
                        std::chrono::milliseconds patience = std::chrono::milliseconds(2000)) {

	const auto deadline = std::chrono::steady_clock::now() + patience;

	for(;;) {

		std::error_code error;
		std::filesystem::rename(from, to, error);
		if(!error) {
			return true;
		}

		if(mode == ReplaceMode::Once || std::chrono::steady_clock::now() >= deadline) {
			return false;
		}

		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
}

} // namespace core

#endif // ARX_CORE_FILEREPLACE_H
