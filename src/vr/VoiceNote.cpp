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

#include "vr/VoiceNote.h"

namespace vr {

namespace {

//! The most samples a note keeps: maxMs of them
constexpr size_t NoteCapacity = size_t(VoiceNote::sampleRate) * size_t(VoiceNote::maxMs) / 1000;

void appendU16(std::string & out, u16 value) {
	out.push_back(char(value & 0xff));
	out.push_back(char((value >> 8) & 0xff));
}

void appendU32(std::string & out, u32 value) {
	appendU16(out, u16(value & 0xffff));
	appendU16(out, u16(value >> 16));
}

} // anonymous namespace

VoiceNote::State VoiceNote::start(NoteDevice & device, s64 timeMs) {

	stop();
	m_samples.clear();
	m_startMs = timeMs;
	if(!device.open(sampleRate)) {
		m_device = nullptr;
		m_state = State::NoDevice;
		return m_state;
	}
	m_device = &device;
	m_state = State::Recording;
	return m_state;
}

VoiceNote::State VoiceNote::update(s64 timeMs, bool held) {

	if(m_state != State::Recording) {
		return m_state;
	}

	if(!held || m_samples.size() >= NoteCapacity || timeMs - m_startMs >= maxMs) {
		stop(); // push-to-talk let go, or the longest note: what the device has is read first
	} else {
		m_device->read(m_samples, NoteCapacity - m_samples.size());
		if(m_samples.size() >= NoteCapacity) {
			stop();
		}
	}
	return m_state;
}

VoiceNote::State VoiceNote::stop() {
	if(m_state == State::Recording) {
		// What the device captured since the last frame belongs to the note too
		if(m_samples.size() < NoteCapacity) {
			m_device->read(m_samples, NoteCapacity - m_samples.size());
		}
		m_device->close();
		m_device = nullptr;
		m_state = State::Finished;
	}
	return m_state;
}

std::string VoiceNote::wav() const {
	// A release too quick to say anything is no note
	if(m_samples.size() < size_t(sampleRate) * size_t(minMs) / 1000) {
		return std::string();
	}
	return formatWav(m_samples.data(), m_samples.size(), sampleRate);
}

std::string formatWav(const s16 * samples, size_t count, u32 sampleRate) {

	const u32 dataBytes = u32(count * 2);
	std::string out;
	out.reserve(44 + size_t(dataBytes));
	out += "RIFF";
	appendU32(out, 36 + dataBytes);
	out += "WAVE";
	out += "fmt ";
	appendU32(out, 16);
	appendU16(out, 1);               // PCM
	appendU16(out, 1);               // mono
	appendU32(out, sampleRate);
	appendU32(out, sampleRate * 2);  // bytes a second
	appendU16(out, 2);               // a block: one 16-bit sample
	appendU16(out, 16);
	out += "data";
	appendU32(out, dataBytes);
	for(size_t i = 0; i < count; i++) {
		appendU16(out, u16(samples[i]));
	}
	return out;
}

} // namespace vr
