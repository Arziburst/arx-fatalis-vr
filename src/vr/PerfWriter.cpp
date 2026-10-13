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
		m_stop = true;
	}
	m_wake.notify_all();
	m_thread.join();
}

void PerfWriter::push(std::string_view line) {
	std::scoped_lock lock(m_mutex);
	if(m_pending.size() + line.size() + 1 > m_maxPending) {
		m_dropped++;
		return;
	}
	m_pending.append(line);
	m_pending.push_back('\n');
	m_pushed++;
}

void PerfWriter::flush() {
	std::unique_lock lock(m_mutex);
	unsigned long long target = m_pushed;
	m_flushWanted = true;
	m_wake.notify_all();
	m_written.wait(lock, [this, target] { return m_done >= target; });
}

size_t PerfWriter::dropped() {
	std::scoped_lock lock(m_mutex);
	return m_dropped;
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
