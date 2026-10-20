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

#include "vr/VoiceNoteTest.h"

#include <algorithm>
#include <string>
#include <vector>

#include "platform/Platform.h"
#include "vr/VoiceNote.h"

CppUnit::AutoRegisterSuite<VoiceNoteTest> g_registerVoiceNoteTest;

using namespace vr;

namespace {

//! 10 s of samples at the note's rate
const size_t VnTenSeconds = size_t(VoiceNote::sampleRate) * 10;

u32 vnU32(const std::string & bytes, size_t at) {
	return u32(u8(bytes[at])) | (u32(u8(bytes[at + 1])) << 8) | (u32(u8(bytes[at + 2])) << 16)
	       | (u32(u8(bytes[at + 3])) << 24);
}

u16 vnU16(const std::string & bytes, size_t at) {
	return u16(u16(u8(bytes[at])) | (u16(u8(bytes[at + 1])) << 8));
}

s16 vnS16(const std::string & bytes, size_t at) {
	return s16(vnU16(bytes, at));
}

/*!
 * A microphone that captures \ref perMs samples a millisecond of the clock the test gives it (a real device's
 * steady stream), or everything \ref burst at once, or nothing (\ref silent); \ref present false: no microphone
 */
class VnFakeDevice final : public NoteDevice {

public:

	bool present = true;
	bool silent = false;
	size_t burst = 0;         //!< samples ready at the first read, on top of the stream
	size_t perMs = 16;        //!< 16 kHz
	s64 now = 0;              //!< the test's clock

	u32 opens = 0;
	u32 closes = 0;
	u32 reads = 0;
	u32 readsWhileClosed = 0;
	u32 openRate = 0;
	bool isOpen = false;

	bool open(u32 sampleRate) override {
		opens++;
		openRate = sampleRate;
		if(!present) {
			return false;
		}
		isOpen = true;
		m_openedAt = now;
		m_delivered = 0;
		return true;
	}

	size_t read(std::vector<s16> & out, size_t max) override {
		reads++;
		if(!isOpen) {
			readsWhileClosed++;
			return 0;
		}
		if(silent) {
			return 0;
		}
		const size_t captured = burst + size_t(std::max<s64>(0, now - m_openedAt)) * perMs;
		const size_t ready = captured > m_delivered ? captured - m_delivered : 0;
		const size_t n = std::min(ready, max);
		for(size_t i = 0; i < n; i++) {
			// A recognisable signal: the sample's index, wrapped
			out.push_back(s16((m_delivered + i) % 30000));
		}
		m_delivered += n;
		return n;
	}

	void close() override {
		closes++;
		isOpen = false;
	}

private:

	s64 m_openedAt = 0;
	size_t m_delivered = 0;

};

//! Run the note's frames every 11 ms from \a from to \a to (inclusive), the device's clock with it
void vnRun(VoiceNote & note, VnFakeDevice & device, s64 from, s64 to) {
	for(s64 t = from; t <= to; t += 11) {
		device.now = t;
		note.update(t);
	}
}

} // anonymous namespace

void VoiceNoteTest::wavHeaderTest() {

	const s16 samples[] = { 0, 1000, -2 };
	const std::string wav = formatWav(samples, 3, 16000);

	CPPUNIT_ASSERT_EQUAL_MESSAGE("44 header bytes and 2 a sample", size_t(44 + 6), wav.size());
	CPPUNIT_ASSERT_EQUAL(std::string("RIFF"), wav.substr(0, 4));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the RIFF size: the file less 8", u32(36 + 6), vnU32(wav, 4));
	CPPUNIT_ASSERT_EQUAL(std::string("WAVE"), wav.substr(8, 4));
	CPPUNIT_ASSERT_EQUAL(std::string("fmt "), wav.substr(12, 4));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the fmt chunk is 16 bytes", u32(16), vnU32(wav, 16));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("PCM", u16(1), vnU16(wav, 20));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("mono", u16(1), vnU16(wav, 22));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the sample rate", u32(16000), vnU32(wav, 24));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("bytes a second", u32(32000), vnU32(wav, 28));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the block: one 16-bit sample", u16(2), vnU16(wav, 32));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("16 bits a sample", u16(16), vnU16(wav, 34));
	CPPUNIT_ASSERT_EQUAL(std::string("data"), wav.substr(36, 4));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the data size", u32(6), vnU32(wav, 40));

	// The samples, little-endian
	CPPUNIT_ASSERT_EQUAL(s16(0), vnS16(wav, 44));
	CPPUNIT_ASSERT_EQUAL(s16(1000), vnS16(wav, 46));
	CPPUNIT_ASSERT_EQUAL(s16(-2), vnS16(wav, 48));
	CPPUNIT_ASSERT_EQUAL(u8(0xe8), u8(wav[46]));
	CPPUNIT_ASSERT_EQUAL(u8(0x03), u8(wav[47]));
	CPPUNIT_ASSERT_EQUAL(u8(0xfe), u8(wav[48]));
	CPPUNIT_ASSERT_EQUAL(u8(0xff), u8(wav[49]));

	// Another rate
	const std::string cd = formatWav(samples, 3, 44100);
	CPPUNIT_ASSERT_EQUAL(u32(44100), vnU32(cd, 24));
	CPPUNIT_ASSERT_EQUAL(u32(88200), vnU32(cd, 28));
}

void VoiceNoteTest::wavEmptyTest() {

	const std::string wav = formatWav(nullptr, 0, 16000);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("no sample: the header alone", size_t(44), wav.size());
	CPPUNIT_ASSERT_EQUAL(u32(36), vnU32(wav, 4));
	CPPUNIT_ASSERT_EQUAL(u32(0), vnU32(wav, 40));
}

void VoiceNoteTest::wavLengthTest() {

	// 10 s at 16 kHz: the length the header gives is 10 s
	std::vector<s16> samples(VnTenSeconds, s16(-1234));
	const std::string wav = formatWav(samples.data(), samples.size(), VoiceNote::sampleRate);
	CPPUNIT_ASSERT_EQUAL(size_t(44) + VnTenSeconds * 2, wav.size());
	CPPUNIT_ASSERT_EQUAL(u32(36 + VnTenSeconds * 2), vnU32(wav, 4));
	CPPUNIT_ASSERT_EQUAL(u32(VnTenSeconds * 2), vnU32(wav, 40));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("data bytes / bytes a second = 10 s", u32(10), vnU32(wav, 40) / vnU32(wav, 28));
	CPPUNIT_ASSERT_EQUAL(s16(-1234), vnS16(wav, wav.size() - 2));

	// An odd count: no padding byte (the data chunk is always even for 16-bit samples)
	std::vector<s16> odd(4001, s16(7));
	CPPUNIT_ASSERT_EQUAL(size_t(44 + 8002), formatWav(odd.data(), odd.size(), 16000).size());
}

void VoiceNoteTest::noDeviceTest() {

	CPPUNIT_ASSERT_EQUAL_MESSAGE("the note is 16 kHz", u32(16000), VoiceNote::sampleRate);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the note is at most 10 s", s64(10000), VoiceNote::maxMs);

	VoiceNote note;
	CPPUNIT_ASSERT(note.state() == VoiceNote::State::Idle);
	CPPUNIT_ASSERT(!note.recording());

	VnFakeDevice device;
	device.present = false;
	device.now = 5000;
	CPPUNIT_ASSERT_MESSAGE("no microphone: start says NoDevice", note.start(device, 5000) == VoiceNote::State::NoDevice);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("... after trying to open it once", u32(1), device.opens);
	CPPUNIT_ASSERT(note.state() == VoiceNote::State::NoDevice);
	CPPUNIT_ASSERT(!note.recording());

	vnRun(note, device, 5011, 17000);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("nothing is read from a device that did not open", u32(0), device.reads);
	CPPUNIT_ASSERT(note.state() == VoiceNote::State::NoDevice);
	CPPUNIT_ASSERT(note.samples().empty());
	CPPUNIT_ASSERT_MESSAGE("no note: no WAV", note.wav().empty());
	CPPUNIT_ASSERT(note.stop() == VoiceNote::State::NoDevice);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("nothing to close", u32(0), device.closes);
}

void VoiceNoteTest::recordsTenSecondsTest() {

	VoiceNote note;
	VnFakeDevice device;
	device.now = 2000;
	CPPUNIT_ASSERT(note.start(device, 2000) == VoiceNote::State::Recording);
	CPPUNIT_ASSERT(note.recording());
	CPPUNIT_ASSERT_EQUAL(u32(1), device.opens);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("opened at the note's rate", VoiceNote::sampleRate, device.openRate);

	// 9.9 s: still recording, reading as it goes
	vnRun(note, device, 2011, 11900);
	CPPUNIT_ASSERT_MESSAGE("still recording at 9.9 s", note.recording());
	CPPUNIT_ASSERT_EQUAL(u32(0), device.closes);
	CPPUNIT_ASSERT_MESSAGE("the samples come in while it records", note.samples().size() >= size_t(16) * 9800);

	// Past 10 s: finished, the device closed once, never more than 10 s
	vnRun(note, device, 11911, 14000);
	CPPUNIT_ASSERT_MESSAGE("finished by 10 s", note.state() == VoiceNote::State::Finished);
	CPPUNIT_ASSERT(!note.recording());
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the device closed once", u32(1), device.closes);
	CPPUNIT_ASSERT_MESSAGE("at most 10 s of samples", note.samples().size() <= VnTenSeconds);
	CPPUNIT_ASSERT_MESSAGE("... and about 10 s", note.samples().size() >= VnTenSeconds - 16 * 50);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("no read once closed", u32(0), device.readsWhileClosed);

	// The samples are the device's, in order
	for(size_t i = 0; i < note.samples().size(); i += 997) {
		CPPUNIT_ASSERT_EQUAL(s16(i % 30000), note.samples()[i]);
	}

	// Later frames change nothing
	const size_t kept = note.samples().size();
	const u32 reads = device.reads;
	vnRun(note, device, 14011, 20000);
	CPPUNIT_ASSERT(note.state() == VoiceNote::State::Finished);
	CPPUNIT_ASSERT_EQUAL(kept, note.samples().size());
	CPPUNIT_ASSERT_EQUAL_MESSAGE("no read after the note finished", reads, device.reads);
	CPPUNIT_ASSERT_EQUAL(u32(1), device.closes);

	// The WAV of the note
	const std::string wav = note.wav();
	CPPUNIT_ASSERT_EQUAL(formatWav(note.samples().data(), note.samples().size(), VoiceNote::sampleRate), wav);
	CPPUNIT_ASSERT_EQUAL(size_t(44) + kept * 2, wav.size());
}

void VoiceNoteTest::sampleCapTest() {

	// A device that hands over 15 s at once (a buffer that filled while the frame stalled): 10 s are kept, it stops
	VoiceNote note;
	VnFakeDevice device;
	device.burst = VnTenSeconds + VnTenSeconds / 2;
	device.now = 0;
	CPPUNIT_ASSERT(note.start(device, 0) == VoiceNote::State::Recording);
	device.now = 11;
	note.update(11);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("never more than 10 s of samples", VnTenSeconds, note.samples().size());
	CPPUNIT_ASSERT_MESSAGE("10 s of samples finish the note, whatever the clock says",
	                       note.state() == VoiceNote::State::Finished);
	CPPUNIT_ASSERT_EQUAL(u32(1), device.closes);
	CPPUNIT_ASSERT_EQUAL(size_t(44) + VnTenSeconds * 2, note.wav().size());
}

void VoiceNoteTest::silentDeviceTest() {

	// A microphone that opens but gives nothing: it stops at 10 s by the clock, and there is no note
	VoiceNote note;
	VnFakeDevice device;
	device.silent = true;
	device.now = 1000;
	CPPUNIT_ASSERT(note.start(device, 1000) == VoiceNote::State::Recording);
	vnRun(note, device, 1011, 10900);
	CPPUNIT_ASSERT(note.recording());
	vnRun(note, device, 10911, 11100);
	CPPUNIT_ASSERT_MESSAGE("10 s of the clock finish the note", note.state() == VoiceNote::State::Finished);
	CPPUNIT_ASSERT_EQUAL(u32(1), device.closes);
	CPPUNIT_ASSERT(note.samples().empty());
	CPPUNIT_ASSERT_MESSAGE("nothing recorded: no WAV, so no note.wav", note.wav().empty());
}

void VoiceNoteTest::stopEarlyTest() {

	VoiceNote note;
	VnFakeDevice device;
	device.now = 0;
	note.start(device, 0);
	vnRun(note, device, 11, 4000);
	CPPUNIT_ASSERT(note.recording());
	CPPUNIT_ASSERT(note.stop() == VoiceNote::State::Finished);
	CPPUNIT_ASSERT(!note.recording());
	CPPUNIT_ASSERT_EQUAL(u32(1), device.closes);
	const size_t kept = note.samples().size();
	CPPUNIT_ASSERT_MESSAGE("about 4 s kept", kept >= size_t(16) * 3900 && kept <= size_t(16) * 4000);
	CPPUNIT_ASSERT_EQUAL(size_t(44) + kept * 2, note.wav().size());

	// Stopped twice, updated after: nothing more
	note.stop();
	vnRun(note, device, 4011, 12000);
	CPPUNIT_ASSERT_EQUAL(u32(1), device.closes);
	CPPUNIT_ASSERT_EQUAL(kept, note.samples().size());
	CPPUNIT_ASSERT_EQUAL(u32(0), device.readsWhileClosed);
}

void VoiceNoteTest::secondNoteTest() {

	// The next mark's note starts empty and gets its own 10 s
	VoiceNote note;
	VnFakeDevice device;
	device.now = 0;
	note.start(device, 0);
	vnRun(note, device, 11, 12000);
	CPPUNIT_ASSERT(note.state() == VoiceNote::State::Finished);

	device.now = 30000;
	CPPUNIT_ASSERT(note.start(device, 30000) == VoiceNote::State::Recording);
	CPPUNIT_ASSERT_EQUAL(u32(2), device.opens);
	CPPUNIT_ASSERT_MESSAGE("the first note's samples are gone", note.samples().empty());
	vnRun(note, device, 30011, 35000);
	CPPUNIT_ASSERT(note.recording());
	CPPUNIT_ASSERT(note.samples().size() <= size_t(16) * 5000);
	vnRun(note, device, 35011, 41000);
	CPPUNIT_ASSERT(note.state() == VoiceNote::State::Finished);
	CPPUNIT_ASSERT_EQUAL(u32(2), device.closes);
	CPPUNIT_ASSERT(note.samples().size() <= VnTenSeconds);
	CPPUNIT_ASSERT(note.samples().size() >= VnTenSeconds - 16 * 50);
}
