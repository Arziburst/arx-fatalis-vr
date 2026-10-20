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

#include "core/ChannelWriter.h"

#include <fstream>
#include <utility>

#include "core/FileReplace.h"

namespace {

bool writeWhole(const std::filesystem::path & file, const std::string & text) {
	std::ofstream out(file, std::ios::binary | std::ios::trunc);
	if(!out) {
		return false;
	}
	out.write(text.data(), std::streamsize(text.size()));
	out.close();
	return bool(out);
}

} // anonymous namespace

ChannelWriter::ChannelWriter(Files files, Landed landed, Warn warn, std::chrono::milliseconds patience)
	: m_files(std::move(files))
	, m_landed(std::move(landed))
	, m_warn(std::move(warn))
	, m_patience(patience)
	, m_thread(&ChannelWriter::run, this)
{ }

ChannelWriter::~ChannelWriter() {
	{
		std::scoped_lock lock(m_mutex);
		handOver();
		m_stop = true;
	}
	m_wake.notify_all();
	m_thread.join();
}

void ChannelWriter::status(std::string text, bool mayKeep, unsigned long long tag) {
	m_heldStatus = std::move(text);
	m_heldTag = tag;
	m_holdsStatus = true;
	if(mayKeep) {
		tryHandOver();
		return;
	}
	{
		std::scoped_lock lock(m_mutex);
		handOver();
	}
	m_wake.notify_one();
}

void ChannelWriter::ack(std::string text) {
	m_heldAck = std::move(text);
	m_holdsAck = true;
	{
		std::scoped_lock lock(m_mutex);
		handOver();
	}
	m_wake.notify_one();
}

void ChannelWriter::poll() {
	if(m_holdsStatus || m_holdsAck) {
		tryHandOver();
	}
}

void ChannelWriter::tryHandOver() {
	// The writer thread holds the lock only to take the texts, but a starved CPU can deschedule it right then: the
	// frame never waits for that, the next call hands them over
	std::unique_lock lock(m_mutex, std::try_to_lock);
	if(!lock.owns_lock()) {
		return;
	}
	handOver();
	lock.unlock();
	m_wake.notify_one();
}

void ChannelWriter::handOver() {
	// The status before the ack: the writer takes both under one lock, so an ack never overtakes its status
	if(m_holdsStatus) {
		m_status = std::move(m_heldStatus);
		m_statusTag = m_heldTag;
		m_heldStatus.clear();
		m_holdsStatus = false;
		m_hasStatus = true;
		m_queued++;
	}
	if(m_holdsAck) {
		m_ack = std::move(m_heldAck);
		m_heldAck.clear();
		m_holdsAck = false;
		m_hasAck = true;
		m_queued++;
	}
}

void ChannelWriter::flush() {
	std::unique_lock lock(m_mutex);
	handOver();
	const unsigned long long target = m_queued;
	m_wake.notify_all();
	m_written.wait(lock, [this, target] { return m_done >= target; });
}

unsigned long long ChannelWriter::statusesWritten() {
	std::scoped_lock lock(m_mutex);
	return m_landedCount;
}

void ChannelWriter::warnOnce(bool & warned, std::string_view message) {
	if(!warned) {
		warned = true;
		if(m_warn) {
			m_warn(message);
		}
	}
}

void ChannelWriter::run() {

	std::string status;
	unsigned long long tag = 0;
	bool hasStatus = false; // a status taken that has not landed yet
	std::string ack;
	bool hasAck = false;    // an ack taken that waits for that status

	std::unique_lock lock(m_mutex);
	while(true) {

		auto work = [this] { return m_stop || m_hasStatus || m_hasAck; };
		if(hasStatus) {
			// A status a reader held for the whole patience: tried again soon, with a newer one if there is one
			m_wake.wait_for(lock, std::chrono::milliseconds(10), work);
		} else {
			m_wake.wait(lock, work);
		}

		if(m_hasStatus) {
			status = std::move(m_status);
			tag = m_statusTag;
			m_status.clear();
			m_hasStatus = false;
			hasStatus = true;
		}
		if(m_hasAck) {
			ack = std::move(m_ack);
			m_ack.clear();
			m_hasAck = false;
			hasAck = true;
		}
		const unsigned long long upto = m_queued;
		const bool stop = m_stop;
		lock.unlock();

		bool landed = false;
		if(hasStatus) {
			if(!writeWhole(m_files.tmp, status)) {
				warnOnce(m_warnedTmp, "could not write " + m_files.tmp.string());
			} else if(core::replaceFile(m_files.tmp, m_files.status, core::ReplaceMode::Patient, m_patience)) {
				landed = true;
				hasStatus = false;
			} else {
				warnOnce(m_warnedHeld, "status.json was held by a reader for the whole patience, the write is tried again");
			}
		}
		if(landed && m_landed) {
			m_landed(tag);
		}
		if(stop) {
			// The last try is over: a status a reader still holds is given up, and its ack written all the same
			hasStatus = false;
		}
		if(hasAck && !hasStatus) {
			if(!writeWhole(m_files.ack, ack) && m_warn) {
				m_warn("could not write " + m_files.ack.string());
			}
			hasAck = false;
		}

		lock.lock();
		if(landed) {
			m_landedCount++;
		}
		if(!hasStatus && !hasAck) {
			m_done = upto;
			m_written.notify_all();
		}
		if(stop && !m_hasStatus && !m_hasAck) {
			break;
		}
	}
}
