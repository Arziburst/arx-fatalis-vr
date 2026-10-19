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

#include "platform/FlightRecorder.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <thread>
#include <vector>

#include "platform/Architecture.h"

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
#include <windows.h>
#endif

namespace platform {

namespace {

// The image is pointer-free and position-independent: everything is at a fixed byte offset from its
// start, so a byte copy at another address - or bytes read from a minidump - serialises the same.
//
//   Header   64 bytes
//   Ring     64-byte header (tid, count, name) + recordsPerThread records
//   Record   64 bytes: a tiny sequence lock, the write number, time, value, kind and 36 chars of text

const u64 Magic = 0x4152585652454331ull; // "ARXVREC1"
const u32 Version = 1;

const size_t HeaderSize = 64;
const size_t RingHeaderSize = 64;
const size_t RecordSize = 64;
const size_t TextCap = 36; // >= 32 kept, the record stays 64 bytes

// Header field offsets
const size_t H_magic = 0;
const size_t H_version = 8;
const size_t H_recordSize = 12;
const size_t H_maxThreads = 16;
const size_t H_recordsPerThread = 20;
const size_t H_ringStride = 24;
const size_t H_originNs = 32;
const size_t H_dropped = 40;

// Ring header field offsets (from the ring's start)
const size_t R_tid = 0;
const size_t R_count = 8;
const size_t R_name = 16;
const size_t R_nameCap = 32;

// Record field offsets (from the record's start)
const size_t D_seq = 0;
const size_t D_wn = 4;
const size_t D_ns = 8;
const size_t D_value = 16;
const size_t D_kind = 24;
const size_t D_textLen = 25;
const size_t D_text = 28;

s64 nowNs() noexcept {
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

u64 currentThreadId() noexcept {
	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	return u64(GetCurrentThreadId());
	#else
	u64 id = u64(std::hash<std::thread::id>()(std::this_thread::get_id()));
	return id ? id : 1;
	#endif
}

// Plain reads of fixed-width fields from a byte image (serialise side; bounds are checked by the caller)
u32 readU32(const unsigned char * p) noexcept {
	u32 value = 0;
	std::memcpy(&value, p, sizeof(value));
	return value;
}
u64 readU64(const unsigned char * p) noexcept {
	u64 value = 0;
	std::memcpy(&value, p, sizeof(value));
	return value;
}
s64 readS64(const unsigned char * p) noexcept {
	s64 value = 0;
	std::memcpy(&value, p, sizeof(value));
	return value;
}

std::atomic<u32> & atomicU32(unsigned char * p) noexcept {
	return *reinterpret_cast<std::atomic<u32> *>(p);
}
std::atomic<u64> & atomicU64(unsigned char * p) noexcept {
	return *reinterpret_cast<std::atomic<u64> *>(p);
}

// A ring cache per thread: one slot, keyed by the recorder's memory. Several recorders thrash it (a
// miss rescans, which is correct), the game's one recorder hits it every frame.
thread_local const void * t_owner = nullptr;
thread_local size_t t_index = 0;
thread_local u64 t_tid = 0;

void appendNumber(std::string & out, s64 value) {
	char buffer[24];
	int n = std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
	if(n > 0) {
		out.append(buffer, size_t(n));
	}
}

void appendUnsigned(std::string & out, u64 value) {
	char buffer[24];
	int n = std::snprintf(buffer, sizeof(buffer), "%llu", static_cast<unsigned long long>(value));
	if(n > 0) {
		out.append(buffer, size_t(n));
	}
}

//! Seconds since the recorder started, 3 decimals, locale-free, never negative
void appendSeconds(std::string & out, s64 deltaNs) {
	if(deltaNs < 0) {
		deltaNs = 0;
	}
	s64 totalMs = deltaNs / 1000000;
	char buffer[32];
	int n = std::snprintf(buffer, sizeof(buffer), "%lld.%03lld",
	                      static_cast<long long>(totalMs / 1000), static_cast<long long>(totalMs % 1000));
	if(n > 0) {
		out.append(buffer, size_t(n));
	}
}

//! Append text with every control character shown as a space, so one record is always one line
void appendText(std::string & out, const char * text, size_t length) {
	for(size_t i = 0; i < length; i++) {
		unsigned char c = static_cast<unsigned char>(text[i]);
		out.push_back((c < 0x20 || c == 0x7f) ? ' ' : char(c));
	}
}

struct Event {
	s64 ns;
	u64 tid;
	u32 wn;
	u8 kind;
	s64 value;
	std::string text;
};

} // anonymous namespace

const char * recordKindName(RecordKind kind) noexcept {
	switch(kind) {
		case RecordKind::Frame:   return "frame";
		case RecordKind::State:   return "state";
		case RecordKind::Command: return "command";
		case RecordKind::Thread:  return "thread";
		case RecordKind::GlError: return "gl";
		case RecordKind::Texture: return "texture";
		case RecordKind::Buffer:  return "buffer";
		case RecordKind::Beat:    return "beat";
		case RecordKind::Exit:    return "exit";
		case RecordKind::Note:    return "note";
		case RecordKind::Count:   break;
	}
	return nullptr;
}

size_t FlightRecorder::memorySize(size_t maxThreads, size_t recordsPerThread) noexcept {
	size_t ringStride = RingHeaderSize + recordsPerThread * RecordSize;
	return HeaderSize + maxThreads * ringStride;
}

FlightRecorder::FlightRecorder(void * memory, size_t size, size_t maxThreads, size_t recordsPerThread) noexcept
	: m_memory(memory)
	, m_size(size)
{
	if(!memory || maxThreads == 0 || recordsPerThread == 0) {
		m_memory = nullptr;
		return;
	}
	size_t needed = memorySize(maxThreads, recordsPerThread);
	if(size < needed) {
		m_memory = nullptr;
		return;
	}

	unsigned char * b = static_cast<unsigned char *>(memory);
	std::memset(b, 0, needed);

	u64 stride = u64(RingHeaderSize + recordsPerThread * RecordSize);
	u64 magic = Magic;
	u32 version = Version;
	u32 recordSize = u32(RecordSize);
	u32 threads = u32(maxThreads);
	u32 perThread = u32(recordsPerThread);
	s64 origin = nowNs();
	std::memcpy(b + H_magic, &magic, sizeof(magic));
	std::memcpy(b + H_version, &version, sizeof(version));
	std::memcpy(b + H_recordSize, &recordSize, sizeof(recordSize));
	std::memcpy(b + H_maxThreads, &threads, sizeof(threads));
	std::memcpy(b + H_recordsPerThread, &perThread, sizeof(perThread));
	std::memcpy(b + H_ringStride, &stride, sizeof(stride));
	std::memcpy(b + H_originNs, &origin, sizeof(origin));
	// The magic is written last and published, so a reader never sees a half-built header
	std::atomic_thread_fence(std::memory_order_release);
	atomicU64(b + H_magic).store(Magic, std::memory_order_release);
}

void FlightRecorder::nameThread(std::string_view name) noexcept {

	if(!m_memory) {
		return;
	}
	unsigned char * b = static_cast<unsigned char *>(m_memory);
	u32 maxThreads = readU32(b + H_maxThreads);
	u64 stride = readU64(b + H_ringStride);

	// Claim (or find) this thread's ring, then name it
	u64 myTid = currentThreadId();
	size_t index = maxThreads; // not found
	for(u32 i = 0; i < maxThreads; i++) {
		if(atomicU64(b + HeaderSize + i * stride + R_tid).load(std::memory_order_acquire) == myTid) {
			index = i;
			break;
		}
	}
	if(index == maxThreads) {
		for(u32 i = 0; i < maxThreads; i++) {
			u64 expected = 0;
			if(atomicU64(b + HeaderSize + i * stride + R_tid).compare_exchange_strong(expected, myTid,
			       std::memory_order_acq_rel)) {
				atomicU64(b + HeaderSize + i * stride + R_count).store(0, std::memory_order_relaxed);
				index = i;
				break;
			}
		}
	}
	if(index == maxThreads) {
		return;
	}

	unsigned char * ring = b + HeaderSize + index * stride;
	std::memset(ring + R_name, 0, R_nameCap);
	std::memcpy(ring + R_name, name.data(), std::min(name.size(), R_nameCap - 1));

	t_owner = m_memory;
	t_index = index;
	t_tid = myTid;
}

void FlightRecorder::record(RecordKind kind, std::string_view text, s64 value) noexcept {

	if(!m_memory) {
		return;
	}
	unsigned char * b = static_cast<unsigned char *>(m_memory);
	u32 maxThreads = readU32(b + H_maxThreads);
	u32 perThread = readU32(b + H_recordsPerThread);
	u64 stride = readU64(b + H_ringStride);
	s64 origin = readS64(b + H_originNs);

	u64 myTid = currentThreadId();

	// Find this thread's ring: the cache first, then a scan, then claim a free one
	size_t index = maxThreads;
	if(t_owner == m_memory && t_tid == myTid
	   && atomicU64(b + HeaderSize + t_index * stride + R_tid).load(std::memory_order_acquire) == myTid) {
		index = t_index;
	} else {
		for(u32 i = 0; i < maxThreads; i++) {
			if(atomicU64(b + HeaderSize + i * stride + R_tid).load(std::memory_order_acquire) == myTid) {
				index = i;
				break;
			}
		}
		if(index == maxThreads) {
			for(u32 i = 0; i < maxThreads; i++) {
				u64 expected = 0;
				if(atomicU64(b + HeaderSize + i * stride + R_tid).compare_exchange_strong(expected, myTid,
				       std::memory_order_acq_rel)) {
					atomicU64(b + HeaderSize + i * stride + R_count).store(0, std::memory_order_relaxed);
					index = i;
					break;
				}
			}
		}
		if(index == maxThreads) {
			atomicU64(b + H_dropped).fetch_add(1, std::memory_order_relaxed);
			return;
		}
		t_owner = m_memory;
		t_index = index;
		t_tid = myTid;
	}

	unsigned char * ring = b + HeaderSize + index * stride;
	std::atomic<u64> & count = atomicU64(ring + R_count);
	u64 c = count.load(std::memory_order_relaxed);
	size_t slot = size_t(c % perThread);
	unsigned char * rec = ring + RingHeaderSize + slot * RecordSize;

	// A sequence lock: seq is odd while this record is being written, even when it is whole. The
	// acq_rel fences keep every field store strictly between the two seq stores, so a reader that saw
	// the same even seq before and after never sees one write's text with another's value.
	std::atomic<u32> & seq = atomicU32(rec + D_seq);
	u32 s = seq.load(std::memory_order_relaxed);
	seq.store(s + 1, std::memory_order_release); // odd: a write is in progress
	std::atomic_thread_fence(std::memory_order_acq_rel);

	u32 wn = u32(c);
	s64 ns = nowNs() - origin;
	u8 kindByte = u8(kind);
	size_t length = std::min(text.size(), TextCap);
	u8 textLen = u8(length);
	std::memcpy(rec + D_wn, &wn, sizeof(wn));
	std::memcpy(rec + D_ns, &ns, sizeof(ns));
	std::memcpy(rec + D_value, &value, sizeof(value));
	rec[D_kind] = kindByte;
	rec[D_textLen] = textLen;
	if(length) {
		std::memcpy(rec + D_text, text.data(), length);
	}

	std::atomic_thread_fence(std::memory_order_acq_rel);
	seq.store(s + 2, std::memory_order_release); // even: the record is whole
	count.store(c + 1, std::memory_order_release);
}

std::string serializeRecorder(const void * image, size_t size) {

	if(!image || size < HeaderSize) {
		return std::string();
	}

	const unsigned char * b = static_cast<const unsigned char *>(image);

	if(readU64(b + H_magic) != Magic) {
		return std::string();
	}
	u32 recordSize = readU32(b + H_recordSize);
	u32 maxThreads = readU32(b + H_maxThreads);
	u32 perThread = readU32(b + H_recordsPerThread);
	u64 stride = readU64(b + H_ringStride);
	if(recordSize != RecordSize || maxThreads == 0 || perThread == 0
	   || stride != u64(RingHeaderSize + u64(perThread) * RecordSize)) {
		return std::string();
	}

	std::string out = "# flight recorder (#545)\n";

	std::vector<std::string> ringHeaders;
	std::vector<Event> events;

	for(u32 i = 0; i < maxThreads; i++) {
		size_t ringOff = HeaderSize + size_t(i) * size_t(stride);
		if(ringOff + RingHeaderSize > size) {
			break; // a cut copy: no more whole ring headers
		}
		u64 tid = readU64(b + ringOff + R_tid);
		if(tid == 0) {
			continue; // this ring was never claimed
		}
		u64 totalCount = readU64(b + ringOff + R_count);
		u64 kept = std::min<u64>(totalCount, perThread);
		u64 overwritten = totalCount - kept;

		std::string header = "# thread ";
		appendUnsigned(header, tid);
		// The name, sanitised to one line (garbage bytes become spaces)
		const char * name = reinterpret_cast<const char *>(b + ringOff + R_name);
		size_t nameLen = 0;
		while(nameLen < R_nameCap - 1 && name[nameLen] != '\0') {
			nameLen++;
		}
		if(nameLen) {
			header.push_back(' ');
			appendText(header, name, nameLen);
		}
		header += ": ";
		appendUnsigned(header, totalCount);
		header += " events, ";
		appendUnsigned(header, overwritten);
		header += " overwritten";
		ringHeaders.push_back(header);

		u64 start = totalCount - kept;
		for(u64 k = start; k < totalCount; k++) {
			size_t slot = size_t(k % perThread);
			size_t recOff = ringOff + RingHeaderSize + slot * RecordSize;
			if(recOff + RecordSize > size) {
				continue; // a cut copy
			}
			unsigned char * rec = const_cast<unsigned char *>(b + recOff);
			std::atomic<u32> & seq = atomicU32(rec + D_seq);
			u32 s1 = seq.load(std::memory_order_acquire);
			if(s1 & 1u) {
				continue; // a write is in progress
			}
			std::atomic_thread_fence(std::memory_order_acq_rel);
			// Snapshot every field, text included, inside the lock: the slot may be overwritten after
			u32 wn = readU32(rec + D_wn);
			s64 ns = readS64(rec + D_ns);
			s64 value = readS64(rec + D_value);
			u8 kind = rec[D_kind];
			size_t textLen = std::min<size_t>(rec[D_textLen], TextCap);
			char textBuffer[TextCap];
			std::memcpy(textBuffer, rec + D_text, textLen);
			std::atomic_thread_fence(std::memory_order_acq_rel);
			u32 s2 = seq.load(std::memory_order_acquire);
			if(s1 != s2) {
				continue; // the record was torn by a concurrent write
			}
			if(wn != u32(k)) {
				continue; // the slot was overwritten since we read the count
			}
			if(kind >= u8(RecordKind::Count)) {
				continue; // an unknown kind: skipped
			}
			Event event;
			event.ns = ns;
			event.tid = tid;
			event.wn = wn;
			event.kind = kind;
			event.value = value;
			appendText(event.text, textBuffer, textLen);
			events.push_back(std::move(event));
		}
	}

	u64 dropped = readU64(b + H_dropped);
	if(dropped != 0) {
		std::string line = "# ";
		appendUnsigned(line, dropped);
		line += " events dropped (threads found no free ring)";
		ringHeaders.push_back(line);
	}

	// Oldest first over all threads; one thread's events keep their order (ns is non-decreasing per
	// thread, wn breaks a tie)
	std::sort(events.begin(), events.end(), [](const Event & a, const Event & c) {
		if(a.ns != c.ns) {
			return a.ns < c.ns;
		}
		if(a.tid != c.tid) {
			return a.tid < c.tid;
		}
		return a.wn < c.wn;
	});

	for(const std::string & header : ringHeaders) {
		out += header;
		out.push_back('\n');
	}
	for(const Event & event : events) {
		appendSeconds(out, event.ns);
		out.push_back(' ');
		appendUnsigned(out, event.tid);
		out.push_back(' ');
		out += recordKindName(RecordKind(event.kind));
		out.push_back(' ');
		appendNumber(out, event.value);
		if(!event.text.empty()) {
			out.push_back(' ');
			out += event.text;
		}
		out.push_back('\n');
	}

	return out;
}

} // namespace platform
