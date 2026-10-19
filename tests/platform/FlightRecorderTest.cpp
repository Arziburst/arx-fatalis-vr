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

#include "platform/FlightRecorderTest.h"

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "platform/FlightRecorder.h"
#include "platform/Platform.h"

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
#include <windows.h>
#endif

CppUnit::AutoRegisterSuite<FlightRecorderTest> g_registerFlightRecorderTest;

// Unique per file in the unity build (PerfStatsTest has a recorderContains() too)
namespace ARX_ANONYMOUS_NAMESPACE {

using platform::RecordKind;

//! One event line of the serialised text
struct RecorderEvent {
	double t = 0.0;
	u64 tid = 0;
	std::string kind;
	s64 value = 0;
	std::string text;
};

bool recorderReadDigits(const std::string & line, size_t & pos) {
	size_t start = pos;
	while(pos < line.size() && line[pos] >= '0' && line[pos] <= '9') {
		pos++;
	}
	return pos > start;
}

//! "<t> <tid> <kind> <value>[ <text>]": t with exactly 3 decimals, kind in lower case letters
bool parseRecorderEvent(const std::string & line, RecorderEvent & event) {

	size_t pos = 0;
	size_t start = pos;
	if(!recorderReadDigits(line, pos) || pos >= line.size() || line[pos] != '.') {
		return false;
	}
	pos++;
	size_t decimals = pos;
	if(!recorderReadDigits(line, pos) || pos - decimals != 3) {
		return false;
	}
	event.t = std::strtod(line.substr(start, pos - start).c_str(), nullptr);

	if(pos >= line.size() || line[pos] != ' ') {
		return false;
	}
	pos++;
	start = pos;
	if(!recorderReadDigits(line, pos)) {
		return false;
	}
	event.tid = std::strtoull(line.substr(start, pos - start).c_str(), nullptr, 10);

	if(pos >= line.size() || line[pos] != ' ') {
		return false;
	}
	pos++;
	start = pos;
	while(pos < line.size() && line[pos] >= 'a' && line[pos] <= 'z') {
		pos++;
	}
	if(pos == start || pos >= line.size() || line[pos] != ' ') {
		return false;
	}
	event.kind = line.substr(start, pos - start);

	pos++;
	start = pos;
	if(pos < line.size() && line[pos] == '-') {
		pos++;
	}
	if(!recorderReadDigits(line, pos)) {
		return false;
	}
	event.value = std::strtoll(line.substr(start, pos - start).c_str(), nullptr, 10);

	if(pos == line.size()) {
		event.text.clear();
		return true;
	}
	if(line[pos] != ' ') {
		return false;
	}
	event.text = line.substr(pos + 1);
	return true;
}

struct RecorderLines {
	std::vector<std::string> comments;
	std::vector<RecorderEvent> events;
	std::vector<std::string> bad; //!< lines that are neither a comment nor an event
};

RecorderLines parseRecorderText(const std::string & text) {
	RecorderLines parsed;
	size_t pos = 0;
	while(pos < text.size()) {
		size_t end = text.find('\n', pos);
		if(end == std::string::npos) {
			end = text.size();
		}
		std::string line = text.substr(pos, end - pos);
		pos = end + 1;
		if(!line.empty() && line.back() == '\r') {
			line.pop_back();
		}
		if(line.empty()) {
			continue;
		}
		if(line[0] == '#') {
			parsed.comments.push_back(line);
			continue;
		}
		RecorderEvent event;
		if(parseRecorderEvent(line, event)) {
			parsed.events.push_back(event);
		} else {
			parsed.bad.push_back(line);
		}
	}
	return parsed;
}

bool recorderStartsWith(const std::string & text, const std::string & prefix) {
	return text.compare(0, prefix.size(), prefix) == 0;
}

bool recorderEndsWith(const std::string & text, const std::string & suffix) {
	return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool recorderContains(const std::string & text, const std::string & part) {
	return text.find(part) != std::string::npos;
}

//! The "# thread <tid>..." header line of a ring, or an empty string
std::string ringHeader(const RecorderLines & parsed, u64 tid) {
	const std::string prefix = "# thread " + std::to_string(tid);
	for(const std::string & line : parsed.comments) {
		if(recorderStartsWith(line, prefix) && (line.size() == prefix.size() || line[prefix.size()] == ' '
		                                || line[prefix.size()] == ':')) {
			return line;
		}
	}
	return std::string();
}

size_t ringHeaderCount(const RecorderLines & parsed) {
	size_t count = 0;
	for(const std::string & line : parsed.comments) {
		if(recorderStartsWith(line, "# thread ")) {
			count++;
		}
	}
	return count;
}

//! A recorder in memory of its own, as the game's lives in the crash handler's shared memory
struct TestRecorder {

	std::vector<unsigned char> memory;
	platform::FlightRecorder recorder;

	TestRecorder(size_t maxThreads, size_t recordsPerThread)
		: memory(platform::FlightRecorder::memorySize(maxThreads, recordsPerThread))
		, recorder(memory.data(), memory.size(), maxThreads, recordsPerThread)
	{ }

	std::string text() const {
		return platform::serializeRecorder(memory.data(), memory.size());
	}

};

std::string recorderNumbered(char prefix, int number, const char * format = "%c-%04d") {
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), format, prefix, number);
	return buffer;
}

} ARX_END_ANONYMOUS_NAMESPACE

void FlightRecorderTest::testKindsAndLineFormat() {

	TestRecorder r(4, 64);
	r.recorder.nameThread("test main");
	r.recorder.record(RecordKind::Frame, std::string_view(), 5123);
	r.recorder.record(RecordKind::State, "game GAMEPLAY");
	r.recorder.record(RecordKind::Command, "exitprocess 143");
	r.recorder.record(RecordKind::Thread, "start KERNEL32!ExitProcess", 143);
	r.recorder.record(RecordKind::GlError, "glTexImage2D", 0x502);
	r.recorder.record(RecordKind::Texture, std::string_view(), 77);
	r.recorder.record(RecordKind::Buffer, std::string_view(), 78);
	r.recorder.record(RecordKind::Beat, "frame");
	r.recorder.record(RecordKind::Exit, "ExitProcess", -1);
	// A line is one event: control characters in a text (a channel command is anyone's input) are spaces
	r.recorder.record(RecordKind::Note, "two\nlines\tend");

	const std::string text = r.text();
	const RecorderLines parsed = parseRecorderText(text);
	CPPUNIT_ASSERT_MESSAGE(text, parsed.bad.empty());
	CPPUNIT_ASSERT_MESSAGE(text, !parsed.comments.empty() && recorderStartsWith(text, "# "));
	CPPUNIT_ASSERT_EQUAL_MESSAGE(text, size_t(10), parsed.events.size());

	const char * kinds[] = { "frame", "state", "command", "thread", "gl", "texture", "buffer", "beat", "exit", "note" };
	const s64 values[] = { 5123, 0, 0, 143, 0x502, 77, 78, 0, -1, 0 };
	const char * texts[] = { "", "game GAMEPLAY", "exitprocess 143", "start KERNEL32!ExitProcess", "glTexImage2D", "", "",
	                         "frame", "ExitProcess", "two lines end" };
	for(size_t i = 0; i < parsed.events.size(); i++) {
		const RecorderEvent & event = parsed.events[i];
		CPPUNIT_ASSERT_EQUAL_MESSAGE(text, std::string(kinds[i]), event.kind);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(text, values[i], event.value);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(text, std::string(texts[i]), event.text);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(text, parsed.events[0].tid, event.tid);
		CPPUNIT_ASSERT_MESSAGE(text, event.t >= 0.0 && event.t < 60.0);
		if(i > 0) {
			CPPUNIT_ASSERT_MESSAGE(text, event.t >= parsed.events[i - 1].t);
		}
	}

	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	// The OS thread id, the one a dump and a debugger show
	CPPUNIT_ASSERT_EQUAL_MESSAGE(text, u64(GetCurrentThreadId()), parsed.events[0].tid);
	#endif

	const std::string header = ringHeader(parsed, parsed.events[0].tid);
	CPPUNIT_ASSERT_MESSAGE(text, recorderContains(header, "test main"));
	CPPUNIT_ASSERT_MESSAGE(text, recorderEndsWith(header, ": 10 events, 0 overwritten"));
	CPPUNIT_ASSERT_EQUAL_MESSAGE(text, size_t(1), ringHeaderCount(parsed));

	CPPUNIT_ASSERT_EQUAL(std::string("frame"), std::string(platform::recordKindName(RecordKind::Frame)));
	CPPUNIT_ASSERT_EQUAL(std::string("state"), std::string(platform::recordKindName(RecordKind::State)));
	CPPUNIT_ASSERT_EQUAL(std::string("command"), std::string(platform::recordKindName(RecordKind::Command)));
	CPPUNIT_ASSERT_EQUAL(std::string("thread"), std::string(platform::recordKindName(RecordKind::Thread)));
	CPPUNIT_ASSERT_EQUAL(std::string("gl"), std::string(platform::recordKindName(RecordKind::GlError)));
	CPPUNIT_ASSERT_EQUAL(std::string("texture"), std::string(platform::recordKindName(RecordKind::Texture)));
	CPPUNIT_ASSERT_EQUAL(std::string("buffer"), std::string(platform::recordKindName(RecordKind::Buffer)));
	CPPUNIT_ASSERT_EQUAL(std::string("beat"), std::string(platform::recordKindName(RecordKind::Beat)));
	CPPUNIT_ASSERT_EQUAL(std::string("exit"), std::string(platform::recordKindName(RecordKind::Exit)));
	CPPUNIT_ASSERT_EQUAL(std::string("note"), std::string(platform::recordKindName(RecordKind::Note)));

}

void FlightRecorderTest::testLongTextIsCut() {

	TestRecorder r(2, 16);
	r.recorder.record(RecordKind::Note, std::string(200, 'x'), 1);
	r.recorder.record(RecordKind::Note, "after", 2);

	const std::string text = r.text();
	const RecorderLines parsed = parseRecorderText(text);
	CPPUNIT_ASSERT_MESSAGE(text, parsed.bad.empty());
	CPPUNIT_ASSERT_EQUAL_MESSAGE(text, size_t(2), parsed.events.size());

	// At least 32 characters kept, the rest cut inside the record
	const std::string & cut = parsed.events[0].text;
	CPPUNIT_ASSERT_MESSAGE(text, cut.size() >= 32 && cut.size() < 64);
	CPPUNIT_ASSERT_MESSAGE(text, cut.find_first_not_of('x') == std::string::npos);
	CPPUNIT_ASSERT_EQUAL_MESSAGE(text, s64(1), parsed.events[0].value);

	// ... and the next record is whole
	CPPUNIT_ASSERT_EQUAL_MESSAGE(text, std::string("after"), parsed.events[1].text);
	CPPUNIT_ASSERT_EQUAL_MESSAGE(text, s64(2), parsed.events[1].value);

}

void FlightRecorderTest::testWrapKeepsTheNewest() {

	TestRecorder r(2, 8);
	for(int i = 0; i < 20; i++) {
		r.recorder.record(RecordKind::Note, recorderNumbered('e', i, "%cvent-%02d"), i);
	}

	const std::string text = r.text();
	const RecorderLines parsed = parseRecorderText(text);
	CPPUNIT_ASSERT_MESSAGE(text, parsed.bad.empty());

	// Exactly the newest 8 of 20, oldest first
	CPPUNIT_ASSERT_EQUAL_MESSAGE(text, size_t(8), parsed.events.size());
	for(size_t k = 0; k < parsed.events.size(); k++) {
		CPPUNIT_ASSERT_EQUAL_MESSAGE(text, recorderNumbered('e', int(12 + k), "%cvent-%02d"), parsed.events[k].text);
		CPPUNIT_ASSERT_EQUAL_MESSAGE(text, s64(12 + k), parsed.events[k].value);
	}

	// The header says how many were recorded and how many are gone
	CPPUNIT_ASSERT_MESSAGE(text, recorderEndsWith(ringHeader(parsed, parsed.events[0].tid), ": 20 events, 12 overwritten"));

}

void FlightRecorderTest::testTwoProducers() {

	int count = 2000;
	TestRecorder r(4, 4096);

	std::atomic<int> ready(0);
	auto produce = [&r, &ready, count](char prefix) {
		ready++;
		while(ready.load() < 2) {
			std::this_thread::yield();
		}
		for(int i = 0; i < count; i++) {
			r.recorder.record(RecordKind::Note, recorderNumbered(prefix, i), i);
		}
	};
	std::thread a(produce, 'a');
	std::thread b(produce, 'b');
	a.join();
	b.join();

	const std::string text = r.text();
	const RecorderLines parsed = parseRecorderText(text);
	CPPUNIT_ASSERT_MESSAGE(text.substr(0, 2000), parsed.bad.empty());
	CPPUNIT_ASSERT_EQUAL(size_t(2 * count), parsed.events.size());

	std::map<char, std::vector<const RecorderEvent *>> byPrefix;
	for(size_t i = 0; i < parsed.events.size(); i++) {
		const RecorderEvent & event = parsed.events[i];
		CPPUNIT_ASSERT_MESSAGE(event.text, event.text.size() == 6 && (event.text[0] == 'a' || event.text[0] == 'b'));
		byPrefix[event.text[0]].push_back(&event);
		// Oldest first over all threads
		if(i > 0) {
			CPPUNIT_ASSERT(event.t >= parsed.events[i - 1].t);
		}
	}

	for(char prefix : { 'a', 'b' }) {
		const std::vector<const RecorderEvent *> & events = byPrefix[prefix];
		CPPUNIT_ASSERT_EQUAL(size_t(count), events.size());
		for(int i = 0; i < count; i++) {
			// Each thread's events whole and in their order, on its own ring
			CPPUNIT_ASSERT_EQUAL(recorderNumbered(prefix, i), events[i]->text);
			CPPUNIT_ASSERT_EQUAL(s64(i), events[i]->value);
			CPPUNIT_ASSERT_EQUAL(events[0]->tid, events[i]->tid);
		}
		CPPUNIT_ASSERT(recorderEndsWith(ringHeader(parsed, events[0]->tid), ": 2000 events, 0 overwritten"));
	}
	CPPUNIT_ASSERT(byPrefix['a'][0]->tid != byPrefix['b'][0]->tid);
	CPPUNIT_ASSERT_EQUAL(size_t(2), ringHeaderCount(parsed));

}

void FlightRecorderTest::testSerialiseWhileWriting() {

	// Small rings that wrap all the time while the main thread reads them, as the dump helper does
	int count = 200000;
	const size_t perThread = 64;
	TestRecorder r(4, perThread);

	std::atomic<bool> go(false);
	std::atomic<int> done(0);
	auto produce = [&r, &go, &done, count](char prefix) {
		while(!go.load()) {
			std::this_thread::yield();
		}
		for(int i = 0; i < count; i++) {
			r.recorder.record(RecordKind::Note, recorderNumbered(prefix, i, "%c-%d"), i);
		}
		done++;
	};
	std::thread a(produce, 'a');
	std::thread b(produce, 'b');
	go = true;

	std::string failure;
	int rounds = 0;
	while(failure.empty() && (done.load() < 2 || rounds == 0)) {
		const std::string text = r.text();
		const RecorderLines parsed = parseRecorderText(text);
		rounds++;
		if(!parsed.bad.empty()) {
			failure = "a malformed line while writing: " + parsed.bad[0];
			break;
		}
		std::map<char, s64> last;
		std::map<char, size_t> seen;
		for(const RecorderEvent & event : parsed.events) {
			if(event.text.size() < 3 || (event.text[0] != 'a' && event.text[0] != 'b') || event.text[1] != '-') {
				failure = "a text nobody wrote: \"" + event.text + "\"";
				break;
			}
			const char prefix = event.text[0];
			// A torn record: the text of one write with the value of another
			if(std::strtoll(event.text.c_str() + 2, nullptr, 10) != event.value) {
				failure = "a torn record: \"" + event.text + "\" with the value " + std::to_string(event.value);
				break;
			}
			if(last.count(prefix) && event.value <= last[prefix]) {
				failure = "out of order: " + std::to_string(event.value) + " after " + std::to_string(last[prefix]);
				break;
			}
			last[prefix] = event.value;
			if(++seen[prefix] > perThread) {
				failure = "more events of one thread than its ring holds";
				break;
			}
		}
	}
	a.join();
	b.join();
	CPPUNIT_ASSERT_MESSAGE(failure, failure.empty());
	CPPUNIT_ASSERT(rounds >= 1);

	// At rest: each ring holds its thread's newest records
	const std::string text = r.text();
	const RecorderLines parsed = parseRecorderText(text);
	CPPUNIT_ASSERT_MESSAGE(text, parsed.bad.empty());
	CPPUNIT_ASSERT_EQUAL_MESSAGE(text, size_t(2 * perThread), parsed.events.size());
	for(char prefix : { 'a', 'b' }) {
		s64 expected = count - s64(perThread);
		for(const RecorderEvent & event : parsed.events) {
			if(event.text[0] == prefix) {
				CPPUNIT_ASSERT_EQUAL_MESSAGE(text, expected, event.value);
				expected++;
			}
		}
		CPPUNIT_ASSERT_EQUAL_MESSAGE(text, s64(count), expected);
	}

}

void FlightRecorderTest::testCopyOfTheImage() {

	TestRecorder r(4, 32);
	r.recorder.nameThread("main thread");
	r.recorder.record(RecordKind::Frame, std::string_view(), 1);
	r.recorder.record(RecordKind::State, "game GAMEPLAY");
	std::thread other([&r] {
		r.recorder.record(RecordKind::Thread, "start arx.exe+0x1234");
	});
	other.join();
	r.recorder.record(RecordKind::Command, "leak 64");

	const std::string live = r.text();
	CPPUNIT_ASSERT_EQUAL_MESSAGE(live, size_t(4), parseRecorderText(live).events.size());

	// The dump helper reads the bytes into its own memory, at another address: no pointers in the image
	std::vector<unsigned char> shifted(r.memory.size() + 8);
	std::memcpy(shifted.data() + 8, r.memory.data(), r.memory.size());
	CPPUNIT_ASSERT_EQUAL(live, platform::serializeRecorder(shifted.data() + 8, r.memory.size()));

	std::vector<unsigned char> copy(r.memory);
	CPPUNIT_ASSERT_EQUAL(live, platform::serializeRecorder(copy.data(), copy.size()));

	// ... and the copy is a snapshot
	r.recorder.record(RecordKind::Note, "later");
	CPPUNIT_ASSERT_EQUAL(live, platform::serializeRecorder(copy.data(), copy.size()));
	CPPUNIT_ASSERT(recorderContains(r.text(), "later"));

}

void FlightRecorderTest::testBrokenImages() {

	TestRecorder r(4, 32);
	for(int i = 0; i < 40; i++) {
		r.recorder.record(RecordKind::Note, "event", i);
	}
	const size_t size = r.memory.size();
	const size_t all = parseRecorderText(r.text()).events.size();
	CPPUNIT_ASSERT_EQUAL(size_t(32), all);

	// Nothing at all
	{
		const RecorderLines parsed = parseRecorderText(platform::serializeRecorder(nullptr, 0));
		CPPUNIT_ASSERT(parsed.events.empty() && parsed.bad.empty());
	}

	// Memory that never held a recorder
	{
		std::vector<unsigned char> zeros(size, 0);
		const RecorderLines parsed = parseRecorderText(platform::serializeRecorder(zeros.data(), zeros.size()));
		CPPUNIT_ASSERT(parsed.events.empty() && parsed.bad.empty());
	}

	// A copy cut off anywhere (a short read of another process)
	for(size_t length = 0; length < size; length += size / 97 + 1) {
		std::vector<unsigned char> cut(r.memory.begin(), r.memory.begin() + std::ptrdiff_t(length));
		const RecorderLines parsed = parseRecorderText(platform::serializeRecorder(cut.data(), cut.size()));
		CPPUNIT_ASSERT_MESSAGE(std::to_string(length), parsed.bad.empty());
		CPPUNIT_ASSERT_MESSAGE(std::to_string(length), parsed.events.size() <= all);
	}

	// Garbage: in the rings behind a good header, and everywhere (the memory of a process that corrupted its heap)
	for(unsigned seed = 1; seed <= 20; seed++) {
		std::mt19937 random(seed);
		std::vector<unsigned char> rings(r.memory);
		for(size_t i = 64; i < rings.size(); i++) {
			rings[i] = static_cast<unsigned char>(random() & 0xff);
		}
		const std::string ringsText = platform::serializeRecorder(rings.data(), rings.size());
		CPPUNIT_ASSERT_MESSAGE(ringsText.substr(0, 1000), parseRecorderText(ringsText).bad.empty());

		std::vector<unsigned char> everything(size);
		for(unsigned char & byte : everything) {
			byte = static_cast<unsigned char>(random() & 0xff);
		}
		const std::string everythingText = platform::serializeRecorder(everything.data(), everything.size());
		CPPUNIT_ASSERT_MESSAGE(everythingText.substr(0, 1000), parseRecorderText(everythingText).bad.empty());
	}

}

void FlightRecorderTest::testThreadWithoutARing() {

	TestRecorder r(1, 16);
	for(int i = 0; i < 3; i++) {
		r.recorder.record(RecordKind::Note, recorderNumbered('m', i), i);
	}
	// The only ring is taken: this thread's records are lost, counted, and never land in the other ring
	std::thread other([&r] {
		for(int i = 0; i < 5; i++) {
			r.recorder.record(RecordKind::Note, "other", i);
		}
	});
	other.join();

	const std::string text = r.text();
	const RecorderLines parsed = parseRecorderText(text);
	CPPUNIT_ASSERT_MESSAGE(text, parsed.bad.empty());
	CPPUNIT_ASSERT_EQUAL_MESSAGE(text, size_t(3), parsed.events.size());
	for(int i = 0; i < 3; i++) {
		CPPUNIT_ASSERT_EQUAL_MESSAGE(text, recorderNumbered('m', i), parsed.events[size_t(i)].text);
	}
	bool counted = false;
	for(const std::string & line : parsed.comments) {
		counted = counted || recorderContains(line, "5 events dropped");
	}
	CPPUNIT_ASSERT_MESSAGE(text, counted);

}

void FlightRecorderTest::testSeveralRecorders() {

	auto texts = [](const TestRecorder & recorder) {
		std::vector<std::string> result;
		for(const RecorderEvent & event : parseRecorderText(recorder.text()).events) {
			result.push_back(event.text);
		}
		return result;
	};

	// Two at once, the same thread writing to both
	{
		TestRecorder a(2, 16);
		a.recorder.record(RecordKind::Note, "in a");
		TestRecorder b(2, 16);
		b.recorder.record(RecordKind::Note, "in b");
		a.recorder.record(RecordKind::Note, "in a again");
		std::thread other([&a, &b] {
			b.recorder.record(RecordKind::Note, "in b from another thread");
			a.recorder.record(RecordKind::Note, "in a from another thread");
		});
		other.join();
		CPPUNIT_ASSERT((texts(a) == std::vector<std::string>{ "in a", "in a again", "in a from another thread" }));
		CPPUNIT_ASSERT((texts(b) == std::vector<std::string>{ "in b", "in b from another thread" }));
	}

	// One after another, each in fresh memory (which may be where the last one was)
	for(int round = 0; round < 3; round++) {
		TestRecorder c(2, 16);
		const std::string name = "round " + std::to_string(round);
		c.recorder.record(RecordKind::Note, name);
		CPPUNIT_ASSERT((texts(c) == std::vector<std::string>{ name }));
	}

}
