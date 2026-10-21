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

#include "vr/EyeShots.h"

#if ARX_HAVE_OPENXR

#include <cstring>

#include "io/log/Logger.h"
#include "platform/Time.h"
#include "vr/PerfLog.h" // the live GL texture count for the resource line (#545)

namespace vr {

namespace {

bool isSrgb(s64 format) {
	return format == GL_SRGB8_ALPHA8 || format == GL_SRGB8;
}

//! The OpenGL state a capture changes, restored afterwards (the renderer caches its own view of it)
class CaptureState {

	GLint m_drawFramebuffer = 0;
	GLint m_readFramebuffer = 0;
	GLint m_readBuffer = 0;
	GLint m_texture = 0;
	GLint m_packBuffer = 0;
	GLint m_packAlignment = 4;
	GLboolean m_scissor = GL_FALSE;
	GLboolean m_srgb = GL_FALSE;

public:

	CaptureState() {
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_drawFramebuffer);
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &m_readFramebuffer);
		glGetIntegerv(GL_READ_BUFFER, &m_readBuffer);
		glGetIntegerv(GL_TEXTURE_BINDING_2D, &m_texture);
		glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &m_packBuffer);
		glGetIntegerv(GL_PACK_ALIGNMENT, &m_packAlignment);
		m_scissor = glIsEnabled(GL_SCISSOR_TEST);
		m_srgb = glIsEnabled(GL_FRAMEBUFFER_SRGB);
	}

	~CaptureState() {
		if(m_srgb) {
			glEnable(GL_FRAMEBUFFER_SRGB);
		} else {
			glDisable(GL_FRAMEBUFFER_SRGB);
		}
		if(m_scissor) {
			glEnable(GL_SCISSOR_TEST);
		} else {
			glDisable(GL_SCISSOR_TEST);
		}
		glPixelStorei(GL_PACK_ALIGNMENT, m_packAlignment);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(m_packBuffer));
		glBindTexture(GL_TEXTURE_2D, GLuint(m_texture));
		glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(m_readFramebuffer));
		glReadBuffer(GLenum(m_readBuffer));
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(m_drawFramebuffer));
	}

	CaptureState(const CaptureState &) = delete;
	CaptureState & operator=(const CaptureState &) = delete;

};

} // anonymous namespace

bool EyeShots::request(u32 id, size_t eye, bool half) {
	// A free slot that held a picture of this kind keeps its texture; else any free one
	Slot * chosen = nullptr;
	for(Slot & slot : m_slots) {
		if(slot.stage == Slot::Stage::Free && (!chosen || (slot.texture && slot.half == half))) {
			chosen = &slot;
		}
	}
	if(!chosen) {
		return false;
	}
	chosen->stage = Slot::Stage::Wanted;
	chosen->id = id;
	chosen->eye = eye;
	chosen->half = half;
	return true;
}

bool EyeShots::prepare(Slot & slot, s64 format) {

	// The texture is kept between pictures: a new one only for another size or format
	if(slot.texture && (slot.format != format || slot.storage != slot.size)) {
		release(slot);
	}
	if(!slot.texture) {
		glGenTextures(1, &slot.texture);
		vr::perf::addTexture();
		glBindTexture(GL_TEXTURE_2D, slot.texture);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
		glTexImage2D(GL_TEXTURE_2D, 0, GLint(format), slot.size.x, slot.size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		slot.storage = slot.size;
		slot.format = format;
		glGenFramebuffers(1, &slot.framebuffer);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, slot.framebuffer);
		glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, slot.texture, 0);
		if(glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			LogWarning << "[vr] eye shot: the copy's framebuffer is incomplete (" << glFormatName(format) << ")";
			release(slot);
			return false;
		}
	}

	const size_t bytes = size_t(slot.size.x) * size_t(slot.size.y) * 3;
	if(!slot.buffer) {
		glGenBuffers(1, &slot.buffer);
		slot.bufferBytes = 0;
	}
	glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.buffer);
	if(slot.bufferBytes < bytes) {
		glBufferData(GL_PIXEL_PACK_BUFFER, GLsizeiptr(bytes), nullptr, GL_STREAM_READ);
		slot.bufferBytes = bytes;
	}
	return true;
}

void EyeShots::capture(size_t eye, GLuint framebuffer, Vec2i size, s64 format) {

	bool wanted = false;
	for(const Slot & slot : m_slots) {
		wanted = wanted || (slot.stage == Slot::Stage::Wanted && slot.eye == eye);
	}
	if(!wanted || size.x <= 0 || size.y <= 0 || !framebuffer) {
		return;
	}

	CaptureState state;
	glDisable(GL_SCISSOR_TEST);
	// An sRGB image into an sRGB copy: decoded, filtered, encoded again - the stored values stay sRGB-encoded
	if(isSrgb(format)) {
		glEnable(GL_FRAMEBUFFER_SRGB);
	} else {
		glDisable(GL_FRAMEBUFFER_SRGB);
	}
	glPixelStorei(GL_PACK_ALIGNMENT, 1);

	for(Slot & slot : m_slots) {
		if(slot.stage != Slot::Stage::Wanted || slot.eye != eye) {
			continue;
		}
		slot.size = slot.half ? Vec2i(size.x / 2, size.y / 2) : size;
		if(slot.size.x <= 0 || slot.size.y <= 0 || !prepare(slot, format)) {
			slot.stage = Slot::Stage::Free;
			continue;
		}
		// Exactly half: each copied pixel samples the corner of its 2x2 block, the linear filter its mean
		const Vec2i source = slot.half ? Vec2i(slot.size.x * 2, slot.size.y * 2) : size;
		glBindFramebuffer(GL_READ_FRAMEBUFFER, framebuffer);
		glReadBuffer(GL_COLOR_ATTACHMENT0);
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, slot.framebuffer);
		glBlitFramebuffer(0, 0, source.x, source.y, 0, 0, slot.size.x, slot.size.y, GL_COLOR_BUFFER_BIT,
		                  slot.half ? GL_LINEAR : GL_NEAREST);
		// The read-back into the pixel buffer returns at once; the fence says when it is there. The texture's stored
		// values, never sRGB-decoded (a texture query converts nothing, a framebuffer read may)
		glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.buffer);
		glBindTexture(GL_TEXTURE_2D, slot.texture);
		glGetTexImage(GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
		slot.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
		slot.stage = Slot::Stage::Reading;
	}
	glFlush();
}

bool EyeShots::take(u32 & id, RgbImage & image, s64 & copyUs) {

	for(Slot & slot : m_slots) {
		if(slot.stage != Slot::Stage::Reading) {
			continue;
		}
		const GLenum done = glClientWaitSync(slot.fence, 0, 0);
		if(done != GL_ALREADY_SIGNALED && done != GL_CONDITION_SATISFIED) {
			if(done == GL_WAIT_FAILED) {
				LogWarning << "[vr] eye shot: the fence failed - picture " << slot.id << " dropped";
				glDeleteSync(slot.fence);
				slot.fence = nullptr;
				slot.stage = Slot::Stage::Free;
			}
			continue;
		}
		glDeleteSync(slot.fence);
		slot.fence = nullptr;

		const PlatformInstant start = platform::getTime();
		GLint bound = 0;
		glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &bound);
		glBindBuffer(GL_PIXEL_PACK_BUFFER, slot.buffer);
		const size_t bytes = size_t(slot.size.x) * size_t(slot.size.y) * 3;
		const void * pixels = glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, GLsizeiptr(bytes), GL_MAP_READ_BIT);
		RgbImage picture;
		if(pixels) {
			picture = RgbImage(size_t(slot.size.x), size_t(slot.size.y));
			std::memcpy(picture.pixels.data(), pixels, bytes);
			glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
		} else {
			LogWarning << "[vr] eye shot: the pixel buffer could not be mapped - picture " << slot.id << " empty";
		}
		glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(bound));
		copyUs = toUs(platform::getTime() - start);

		id = slot.id;
		image = std::move(picture);
		slot.stage = Slot::Stage::Free;
		return true;
	}
	return false;
}

void EyeShots::cancel() {
	for(Slot & slot : m_slots) {
		if(slot.fence) {
			glDeleteSync(slot.fence);
			slot.fence = nullptr;
		}
		slot.stage = Slot::Stage::Free;
	}
}

size_t EyeShots::pending() const {
	size_t count = 0;
	for(const Slot & slot : m_slots) {
		count += slot.stage != Slot::Stage::Free ? 1 : 0;
	}
	return count;
}

void EyeShots::release(Slot & slot) {
	if(slot.framebuffer) {
		glDeleteFramebuffers(1, &slot.framebuffer);
		slot.framebuffer = 0;
	}
	if(slot.texture) {
		glDeleteTextures(1, &slot.texture);
		vr::perf::removeTexture();
		slot.texture = 0;
	}
	slot.storage = Vec2i(0);
	slot.format = 0;
}

void EyeShots::destroy() {
	cancel();
	for(Slot & slot : m_slots) {
		release(slot);
		if(slot.buffer) {
			glDeleteBuffers(1, &slot.buffer);
			slot.buffer = 0;
			slot.bufferBytes = 0;
		}
	}
}

} // namespace vr

#endif // ARX_HAVE_OPENXR
