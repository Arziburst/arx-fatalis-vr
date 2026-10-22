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

#ifndef ARX_VR_EYESHOTS_H
#define ARX_VR_EYESHOTS_H

#include "vr/OpenXRUtil.h"

#if ARX_HAVE_OPENXR

#include <array>

#include "math/Types.h"
#include "vr/EyeImage.h"

namespace vr {

/*!
 * \brief Pictures of the eyes for the marks of the moment, without stalling the frame (#574)
 *
 * A picture is asked for (\ref request), taken from the next frame that draws that eye (\ref capture: a blit of the
 * eye's image into a texture of our own - at full size, or halved with a linear filter, each pixel the mean of a 2x2
 * block - then a read-back into a pixel buffer, fenced), and handed over once the GPU is done (\ref take: a later
 * frame maps the buffer and copies it out). No call waits for the GPU. The stored values are copied, sRGB-encoded
 * like the window, rows bottom-up (vr/EyeImage.h).
 *
 * Creating and destroying needs the session's OpenGL context: \ref destroy runs with it, not from a destructor.
 */
class EyeShots {

public:

	/*!
	 * Ask for the picture of eye \a eye (0 left, 1 right) of the next frame that draws it, \a half the size or full;
	 * \a id names it in \ref take. \return false when every slot is busy (nothing is asked for)
	 */
	bool request(u32 id, size_t eye, bool half);

	/*!
	 * Eye \a eye of this frame is drawn: \a framebuffer has its image as the colour attachment (single-sample, the
	 * multisampled picture already resolved), \a size pixels, internal format \a format. Copies every picture
	 * asked for of this eye on the GPU and starts its read-back; restores the OpenGL state it changes
	 */
	void capture(size_t eye, GLuint framebuffer, Vec2i size, s64 format);

	/*!
	 * One picture whose read-back finished, moved into \a image with its \a id; false when none has (at most one a
	 * call, so the copies of several pictures spread over frames). \a copyUs gets how long the map and copy took
	 */
	bool take(u32 & id, RgbImage & image, s64 & copyUs);

	//! Forget every picture asked for or on its way
	void cancel();

	//! Pictures asked for and not yet taken
	size_t pending() const;

	//! Delete the OpenGL objects (the session ends, its context still current)
	void destroy();

private:

	struct Slot {
		enum class Stage { Free, Wanted, Reading, Ready };
		Stage stage = Stage::Free;
		u32 id = 0;
		size_t eye = 0;
		bool half = false;
		Vec2i size = Vec2i(0);       //!< the picture's size
		Vec2i storage = Vec2i(0);    //!< the texture's size (kept between pictures)
		s64 format = 0;              //!< the texture's internal format
		GLuint texture = 0;
		GLuint framebuffer = 0;
		GLuint buffer = 0;           //!< the pixel buffer the read-back lands in
		size_t bufferBytes = 0;
		GLsync fence = nullptr;
	};

	//! Two full eyes at the press, a periodic snapshot and the release's
	static constexpr size_t SlotCount = 4;

	bool prepare(Slot & slot, s64 format);
	void release(Slot & slot);

	std::array<Slot, SlotCount> m_slots;

};

} // namespace vr

#endif // ARX_HAVE_OPENXR

#endif // ARX_VR_EYESHOTS_H
