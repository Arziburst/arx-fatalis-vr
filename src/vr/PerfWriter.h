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

#ifndef ARX_VR_PERFWRITER_H
#define ARX_VR_PERFWRITER_H

#include <stddef.h>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>

/*!
 * \brief Writes the performance log off the frame path (#327)
 *
 * The frame thread only appends a line to a memory buffer under a mutex that the writer thread
 * holds for nothing but swapping that buffer out; the sink (the file, with its flush) runs on the
 * writer thread without the lock, so a slow disk never stalls a frame. A flush per line was a
 * measured stutter source in Dishonored VR (research row F-92). When the sink falls so far behind
 * that \a maxPending bytes wait, further lines are dropped and counted instead of growing without
 * bound - the log is evidence, not a reason for a hitch of its own.
 */
class PerfWriter {

public:

	//! Receives a batch of whole lines, each ending in '\n'; called on the writer thread only
	using Sink = std::function<void(std::string_view)>;

	explicit PerfWriter(Sink sink, std::chrono::milliseconds interval = std::chrono::seconds(1),
	                    size_t maxPending = 4 * 1024 * 1024);

	//! Writes what is pending and stops the writer thread
	~PerfWriter();

	PerfWriter(const PerfWriter &) = delete;
	PerfWriter & operator=(const PerfWriter &) = delete;

	//! Queue one line (without '\n'); never waits for the sink
	void push(std::string_view line);

	//! Wait until every line pushed so far went to the sink
	void flush();

	//! Lines dropped because the sink fell behind
	size_t dropped();

private:

	void run();

	Sink m_sink;
	std::chrono::milliseconds m_interval;
	size_t m_maxPending;

	std::mutex m_mutex;
	std::condition_variable m_wake;    //!< the writer thread: a flush is wanted, or stop
	std::condition_variable m_written; //!< flush(): a batch went to the sink
	std::string m_pending;
	size_t m_dropped = 0;
	unsigned long long m_pushed = 0;   //!< batches are counted in pushed lines
	unsigned long long m_done = 0;
	bool m_flushWanted = false;
	bool m_stop = false;
	std::thread m_thread;

};

#endif // ARX_VR_PERFWRITER_H
