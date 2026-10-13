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

#include "vr/PerfWriter.h"

#include <utility>

PerfWriter::PerfWriter(Sink sink, std::chrono::milliseconds interval, size_t maxPending)
	: m_sink(std::move(sink))
	, m_interval(interval)
	, m_maxPending(maxPending)
	, m_thread(&PerfWriter::run, this)
{ }

PerfWriter::~PerfWriter() {
	{
		std::scoped_lock lock(m_mutex);
		handOver();
		m_stop = true;
	}
	m_wake.notify_all();
	m_thread.join();
}

void PerfWriter::push(std::string_view line) {

	// The writer thread holds the lock only to swap the buffer, but a starved CPU can deschedule it
	// right then: the frame never waits for that, it keeps the line and hands it over next time
	std::unique_lock lock(m_mutex, std::try_to_lock);
	if(!lock.owns_lock()) {
		if(m_held.size() + line.size() + 1 > m_maxPending) {
			m_heldDropped++;
			return;
		}
		m_held.append(line);
		m_held.push_back('\n');
		m_heldLines++;
		return;
	}

	handOver();

	if(m_pending.size() + line.size() + 1 > m_maxPending) {
		m_dropped++;
		return;
	}
	m_pending.append(line);
	m_pending.push_back('\n');
	m_pushed++;
}

void PerfWriter::handOver() {
	m_dropped += m_heldDropped;
	m_heldDropped = 0;
	if(m_held.empty()) {
		return;
	}
	if(m_pending.size() + m_held.size() > m_maxPending) {
		m_dropped += m_heldLines;
	} else {
		m_pending.append(m_held);
		m_pushed += m_heldLines;
	}
	m_held.clear();
	m_heldLines = 0;
}

void PerfWriter::flush() {
	std::unique_lock lock(m_mutex);
	handOver();
	unsigned long long target = m_pushed;
	m_flushWanted = true;
	m_wake.notify_all();
	m_written.wait(lock, [this, target] { return m_done >= target; });
}

size_t PerfWriter::dropped() {
	std::scoped_lock lock(m_mutex);
	return m_dropped + m_heldDropped;
}

void PerfWriter::run() {

	std::string batch;

	std::unique_lock lock(m_mutex);
	while(true) {

		m_wake.wait_for(lock, m_interval, [this] { return m_stop || m_flushWanted; });

		batch.clear();
		batch.swap(m_pending);
		unsigned long long upto = m_pushed;
		m_flushWanted = false;
		bool stop = m_stop;

		if(!batch.empty()) {
			// The frame thread keeps pushing into the fresh buffer meanwhile
			lock.unlock();
			m_sink(batch);
			lock.lock();
		}

		m_done = upto;
		m_written.notify_all();

		if(stop && m_pending.empty()) {
			break;
		}
	}
}
