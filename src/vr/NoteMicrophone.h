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

#ifndef ARX_VR_NOTEMICROPHONE_H
#define ARX_VR_NOTEMICROPHONE_H

#include <memory>

#include "vr/VoiceNote.h"

namespace vr {

/*!
 * The default microphone for the voice note of a mark (#551): OpenAL's capture extension, mono 16-bit samples,
 * independent of the game's playback device. No microphone (or a build without OpenAL) opens nothing and logs one
 * line - no note, no error.
 */
std::unique_ptr<NoteDevice> defaultMicrophone();

} // namespace vr

#endif // ARX_VR_NOTEMICROPHONE_H
