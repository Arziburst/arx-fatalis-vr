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

//! 5 minutes of samples at the note's rate: the longest note (the owner)
const size_t VnMaxSamples = size_t(VoiceNote::sampleRate) * 300;

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

//! Run the note's frames every 11 ms from \a from to \a to (inclusive), both trackpads held, the device's clock
//! with it
void vnRun(VoiceNote & note, VnFakeDevice & device, s64 from, s64 to) {
	for(s64 t = from; t <= to; t += 11) {
		device.now = t;
		note.update(t, true);
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

	// 5 minutes at 16 kHz mono 16 bit (the longest note): 9 600 000 data bytes, about 9.6 MB, and the header says 300 s
	std::vector<s16> samples(VnMaxSamples, s16(-1234));
	const std::string wav = formatWav(samples.data(), samples.size(), VoiceNote::sampleRate);
	CPPUNIT_ASSERT_EQUAL(size_t(4800000), VnMaxSamples);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("a 5-minute note.wav is 9 600 044 bytes", size_t(9600044), wav.size());
	CPPUNIT_ASSERT_EQUAL(u32(36 + 9600000), vnU32(wav, 4));
	CPPUNIT_ASSERT_EQUAL(u32(9600000), vnU32(wav, 40));
	CPPUNIT_ASSERT_EQUAL_MESSAGE("data bytes / bytes a second = 300 s", u32(300), vnU32(wav, 40) / vnU32(wav, 28));
	CPPUNIT_ASSERT_EQUAL(s16(-1234), vnS16(wav, wav.size() - 2));

	// 10 s still works the same way
	std::vector<s16> ten(size_t(VoiceNote::sampleRate) * 10, s16(5));
	const std::string tenWav = formatWav(ten.data(), ten.size(), VoiceNote::sampleRate);
	CPPUNIT_ASSERT_EQUAL(size_t(44 + 320000), tenWav.size());
	CPPUNIT_ASSERT_EQUAL(u32(10), vnU32(tenWav, 40) / vnU32(tenWav, 28));

	// An odd count: no padding byte (the data chunk is always even for 16-bit samples)
	std::vector<s16> odd(4001, s16(7));
	CPPUNIT_ASSERT_EQUAL(size_t(44 + 8002), formatWav(odd.data(), odd.size(), 16000).size());
}

void VoiceNoteTest::noDeviceTest() {

	CPPUNIT_ASSERT_EQUAL_MESSAGE("the note is 16 kHz", u32(16000), VoiceNote::sampleRate);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the note is at most 5 minutes (the owner)", s64(300000), VoiceNote::maxMs);

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

void VoiceNoteTest::recordsFiveMinutesTest() {

	VoiceNote note;
	VnFakeDevice device;
	device.now = 2000;
	CPPUNIT_ASSERT(note.start(device, 2000) == VoiceNote::State::Recording);
	CPPUNIT_ASSERT(note.recording());
	CPPUNIT_ASSERT_EQUAL(u32(1), device.opens);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("opened at the note's rate", VoiceNote::sampleRate, device.openRate);

	// Past the old 10 s: still recording while held
	vnRun(note, device, 2011, 12500);
	CPPUNIT_ASSERT_MESSAGE("still recording at 10.5 s (the limit is 5 minutes now)", note.recording());

	// 4:59.9: still recording, reading as it goes
	vnRun(note, device, 12511, 301900);
	CPPUNIT_ASSERT_MESSAGE("still recording at 4:59.9", note.recording());
	CPPUNIT_ASSERT_EQUAL(u32(0), device.closes);
	CPPUNIT_ASSERT_MESSAGE("the samples come in while it records", note.samples().size() >= size_t(16) * 299800);

	// Past 5 minutes, still held: finished by itself, the device closed once, never more than 5 minutes
	vnRun(note, device, 301911, 304000);
	CPPUNIT_ASSERT_MESSAGE("finished by 5 minutes", note.state() == VoiceNote::State::Finished);
	CPPUNIT_ASSERT(!note.recording());
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the device closed once", u32(1), device.closes);
	CPPUNIT_ASSERT_MESSAGE("at most 5 minutes of samples", note.samples().size() <= VnMaxSamples);
	CPPUNIT_ASSERT_MESSAGE("... and about 5 minutes", note.samples().size() >= VnMaxSamples - 16 * 50);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("no read once closed", u32(0), device.readsWhileClosed);

	// The samples are the device's, in order
	for(size_t i = 0; i < note.samples().size(); i += 9973) {
		CPPUNIT_ASSERT_EQUAL(s16(i % 30000), note.samples()[i]);
	}

	// Later frames change nothing
	const size_t kept = note.samples().size();
	const u32 reads = device.reads;
	vnRun(note, device, 304011, 310000);
	CPPUNIT_ASSERT(note.state() == VoiceNote::State::Finished);
	CPPUNIT_ASSERT_EQUAL(kept, note.samples().size());
	CPPUNIT_ASSERT_EQUAL_MESSAGE("no read after the note finished", reads, device.reads);
	CPPUNIT_ASSERT_EQUAL(u32(1), device.closes);

	// The WAV of the note: about 9.6 MB
	const std::string wav = note.wav();
	CPPUNIT_ASSERT_EQUAL(formatWav(note.samples().data(), note.samples().size(), VoiceNote::sampleRate), wav);
	CPPUNIT_ASSERT_EQUAL(size_t(44) + kept * 2, wav.size());
	CPPUNIT_ASSERT(wav.size() > size_t(9500000) && wav.size() <= size_t(9600044));
}

void VoiceNoteTest::sampleCapTest() {

	// A device that hands over 7.5 minutes at once (a buffer that filled while the frame stalled): 5 minutes are
	// kept, it stops
	VoiceNote note;
	VnFakeDevice device;
	device.burst = VnMaxSamples + VnMaxSamples / 2;
	device.now = 0;
	CPPUNIT_ASSERT(note.start(device, 0) == VoiceNote::State::Recording);
	device.now = 11;
	note.update(11, true);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("never more than 5 minutes of samples", VnMaxSamples, note.samples().size());
	CPPUNIT_ASSERT_MESSAGE("5 minutes of samples finish the note, whatever the clock says",
	                       note.state() == VoiceNote::State::Finished);
	CPPUNIT_ASSERT_EQUAL(u32(1), device.closes);
	CPPUNIT_ASSERT_EQUAL(size_t(9600044), note.wav().size());
}

void VoiceNoteTest::silentDeviceTest() {

	// A microphone that opens but gives nothing: it stops at 5 minutes by the clock, and there is no note
	VoiceNote note;
	VnFakeDevice device;
	device.silent = true;
	device.now = 1000;
	CPPUNIT_ASSERT(note.start(device, 1000) == VoiceNote::State::Recording);
	vnRun(note, device, 1011, 300900);
	CPPUNIT_ASSERT(note.recording());
	vnRun(note, device, 300911, 301100);
	CPPUNIT_ASSERT_MESSAGE("5 minutes of the clock finish the note", note.state() == VoiceNote::State::Finished);
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

	// The next mark's note starts empty and gets its own 5 minutes
	VoiceNote note;
	VnFakeDevice device;
	device.now = 0;
	note.start(device, 0);
	vnRun(note, device, 11, 12000);
	device.now = 12011;
	note.update(12011, false);
	CPPUNIT_ASSERT(note.state() == VoiceNote::State::Finished);

	device.now = 30000;
	CPPUNIT_ASSERT(note.start(device, 30000) == VoiceNote::State::Recording);
	CPPUNIT_ASSERT_EQUAL(u32(2), device.opens);
	CPPUNIT_ASSERT_MESSAGE("the first note's samples are gone", note.samples().empty());
	vnRun(note, device, 30011, 35000);
	CPPUNIT_ASSERT(note.recording());
	CPPUNIT_ASSERT(note.samples().size() <= size_t(16) * 5000);
	vnRun(note, device, 35011, 332000);
	CPPUNIT_ASSERT(note.state() == VoiceNote::State::Finished);
	CPPUNIT_ASSERT_EQUAL(u32(2), device.closes);
	CPPUNIT_ASSERT(note.samples().size() <= VnMaxSamples);
	CPPUNIT_ASSERT(note.samples().size() >= VnMaxSamples - 16 * 50);
}

void VoiceNoteTest::releaseStopsTest() {

	CPPUNIT_ASSERT_EQUAL_MESSAGE("the shortest note kept is 0.3 s", s64(300), VoiceNote::minMs);

	// Held 4 s, then either trackpad let go: the note stops on that update, the device closed once
	VoiceNote note;
	VnFakeDevice device;
	device.now = 1000;
	CPPUNIT_ASSERT(note.start(device, 1000) == VoiceNote::State::Recording);
	vnRun(note, device, 1011, 5000);
	CPPUNIT_ASSERT(note.recording());
	CPPUNIT_ASSERT_EQUAL(u32(0), device.closes);

	device.now = 5011;
	CPPUNIT_ASSERT_MESSAGE("released: Finished on that very update",
	                       note.update(5011, false) == VoiceNote::State::Finished);
	CPPUNIT_ASSERT(!note.recording());
	CPPUNIT_ASSERT_EQUAL_MESSAGE("the device closed at the release", u32(1), device.closes);
	const size_t kept = note.samples().size();
	CPPUNIT_ASSERT_MESSAGE("about the 4 s held", kept >= size_t(16) * 3950 && kept <= size_t(16) * 4011);
	CPPUNIT_ASSERT_EQUAL(size_t(44) + kept * 2, note.wav().size());

	// Held again afterwards (the next frames of the same press, or a new one): nothing more - only a mark starts a note
	const u32 reads = device.reads;
	vnRun(note, device, 5022, 9000);
	CPPUNIT_ASSERT(note.state() == VoiceNote::State::Finished);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("no read after the release", reads, device.reads);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("never opened again by itself", u32(1), device.opens);
	CPPUNIT_ASSERT_EQUAL(u32(1), device.closes);
	CPPUNIT_ASSERT_EQUAL(kept, note.samples().size());
	CPPUNIT_ASSERT_EQUAL(u32(0), device.readsWhileClosed);
}

void VoiceNoteTest::shortHoldTest() {

	// Let go 0.2 s after the mark: a few samples, below minMs - no WAV, so no note.wav
	{
		VoiceNote note;
		VnFakeDevice device;
		device.now = 0;
		note.start(device, 0);
		vnRun(note, device, 11, 198);
		device.now = 209;
		CPPUNIT_ASSERT(note.update(209, false) == VoiceNote::State::Finished);
		CPPUNIT_ASSERT_EQUAL(u32(1), device.closes);
		CPPUNIT_ASSERT_MESSAGE("some samples were read", !note.samples().empty());
		CPPUNIT_ASSERT_MESSAGE("a 0.2 s note is no note: no WAV", note.wav().empty());
	}

	// Let go on the first update after the mark (a quick press and release): no WAV
	{
		VoiceNote note;
		VnFakeDevice device;
		device.now = 0;
		note.start(device, 0);
		device.now = 11;
		CPPUNIT_ASSERT(note.update(11, false) == VoiceNote::State::Finished);
		CPPUNIT_ASSERT(!note.recording());
		CPPUNIT_ASSERT_EQUAL(u32(1), device.closes);
		CPPUNIT_ASSERT_MESSAGE("a quick press and release is no note: no WAV", note.wav().empty());
		vnRun(note, device, 22, 3000);
		CPPUNIT_ASSERT_MESSAGE("... and nothing records after it", note.wav().empty());
		CPPUNIT_ASSERT_EQUAL(u32(0), device.readsWhileClosed);
	}

	// No microphone and a quick release: NoDevice stays, nothing to close, no WAV
	{
		VoiceNote note;
		VnFakeDevice device;
		device.present = false;
		CPPUNIT_ASSERT(note.start(device, 0) == VoiceNote::State::NoDevice);
		CPPUNIT_ASSERT(note.update(11, false) == VoiceNote::State::NoDevice);
		CPPUNIT_ASSERT_EQUAL(u32(0), device.reads);
		CPPUNIT_ASSERT_EQUAL(u32(0), device.closes);
		CPPUNIT_ASSERT(note.wav().empty());
	}
}

void VoiceNoteTest::minLengthTest() {

	// The edge, with a device that has its samples ready at once (no stream): minMs of samples is a note, one less
	// is none
	const size_t minSamples = size_t(VoiceNote::sampleRate) * size_t(VoiceNote::minMs) / 1000;
	CPPUNIT_ASSERT_EQUAL(size_t(4800), minSamples);
	for(size_t ready : { minSamples - 1, minSamples, minSamples + 1 }) {
		VoiceNote note;
		VnFakeDevice device;
		device.perMs = 0;
		device.burst = ready;
		device.now = 0;
		note.start(device, 0);
		device.now = 11;
		note.update(11, false);
		CPPUNIT_ASSERT_EQUAL_MESSAGE("the release reads what the device already has", ready, note.samples().size());
		if(ready < minSamples) {
			CPPUNIT_ASSERT_MESSAGE("one sample short of 0.3 s: no WAV", note.wav().empty());
		} else {
			CPPUNIT_ASSERT_EQUAL_MESSAGE("0.3 s or more: the WAV", size_t(44) + ready * 2, note.wav().size());
		}
	}
}

void VoiceNoteTest::neverWithoutHoldTest() {

	// Never started: frames held or not open nothing
	VoiceNote note;
	VnFakeDevice device;
	for(s64 t = 0; t < 2000; t += 11) {
		device.now = t;
		note.update(t, (t / 500) % 2 == 0);
	}
	CPPUNIT_ASSERT(note.state() == VoiceNote::State::Idle);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("no note started: the microphone never opens", u32(0), device.opens);
	CPPUNIT_ASSERT_EQUAL(u32(0), device.reads);
	CPPUNIT_ASSERT(note.wav().empty());

	// Started, then released and pressed again within the 5 minutes: the note does not resume
	device.now = 2000;
	note.start(device, 2000);
	vnRun(note, device, 2011, 3000);
	device.now = 3011;
	note.update(3011, false);
	const size_t kept = note.samples().size();
	vnRun(note, device, 3022, 6000);
	CPPUNIT_ASSERT(note.state() == VoiceNote::State::Finished);
	CPPUNIT_ASSERT_EQUAL_MESSAGE("pressed again: the note does not resume", kept, note.samples().size());
	CPPUNIT_ASSERT_EQUAL(u32(1), device.opens);
	CPPUNIT_ASSERT_EQUAL(u32(1), device.closes);
}
