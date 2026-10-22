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

#include "vr/NoteMicrophone.h"

#include <algorithm>
#include <vector>

#include "Configure.h"

#if ARX_HAVE_OPENAL
#include <al.h>
#include <alc.h>
#endif

#include "io/log/Logger.h"

namespace vr {

namespace {

#if ARX_HAVE_OPENAL

//! The default capture device of OpenAL
class OpenALMicrophone final : public NoteDevice {

public:

	~OpenALMicrophone() override {
		close();
	}

	bool open(u32 sampleRate) override {
		close();
		if(!alcIsExtensionPresent(nullptr, "ALC_EXT_CAPTURE")) {
			LogInfo << "[vr] note: no OpenAL capture support - no voice note";
			return false;
		}
		// Two seconds of buffer: a frame that stalls loses nothing
		m_device = alcCaptureOpenDevice(nullptr, ALCuint(sampleRate), AL_FORMAT_MONO16, ALCsizei(sampleRate * 2));
		if(!m_device) {
			LogInfo << "[vr] note: no microphone - no voice note";
			return false;
		}
		alcCaptureStart(m_device);
		return true;
	}

	size_t read(std::vector<s16> & out, size_t max) override {
		if(!m_device || max == 0) {
			return 0;
		}
		ALCint available = 0;
		alcGetIntegerv(m_device, ALC_CAPTURE_SAMPLES, 1, &available);
		const size_t count = std::min(size_t(std::max<ALCint>(available, 0)), max);
		if(count == 0) {
			return 0;
		}
		const size_t start = out.size();
		out.resize(start + count);
		alcCaptureSamples(m_device, out.data() + start, ALCsizei(count));
		return count;
	}

	void close() override {
		if(m_device) {
			alcCaptureStop(m_device);
			alcCaptureCloseDevice(m_device);
			m_device = nullptr;
		}
	}

private:

	ALCdevice * m_device = nullptr;

};

#else // !ARX_HAVE_OPENAL

//! A microphone that is never there: a build without OpenAL
class NoMicrophone final : public NoteDevice {

public:

	bool open(u32 /* sampleRate */) override {
		LogInfo << "[vr] note: built without OpenAL - no voice note";
		return false;
	}

	size_t read(std::vector<s16> & /* out */, size_t /* max */) override { return 0; }

	void close() override { }

};

#endif // !ARX_HAVE_OPENAL

} // anonymous namespace

std::unique_ptr<NoteDevice> defaultMicrophone() {
	#if ARX_HAVE_OPENAL
	return std::make_unique<OpenALMicrophone>();
	#else
	return std::make_unique<NoMicrophone>();
	#endif
}

} // namespace vr
