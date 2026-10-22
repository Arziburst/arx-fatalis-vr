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

#include "platform/ProcessResourcesTest.h"

#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "platform/Platform.h"
#include "platform/ProcessResources.h"

#if ARX_PLATFORM == ARX_PLATFORM_WIN32
#include <windows.h>
#endif

CppUnit::AutoRegisterSuite<ProcessResourcesTest> g_registerProcessResourcesTest;

void ProcessResourcesTest::testLineFormat() {

	platform::ResourceSample sample;
	sample.second = 35;
	sample.process.privateMB = 412.54;
	sample.process.workingSetMB = 380.0;
	sample.process.commitMB = 16402.26;
	sample.process.handles = 812;
	sample.process.gdiObjects = 45;
	sample.process.userObjects = 31;
	sample.process.threads = 42;
	sample.textures = 1830;
	sample.buffers = 210;
	sample.swapchains = 6;
	CPPUNIT_ASSERT_EQUAL(std::string("R t=35 priv=412.5 ws=380.0 commit=16402.3 handles=812 gdi=45 user=31 threads=42"
	                                 " tex=1830 buf=210 xrsc=6"), platform::formatResources(sample));

	// Flat, nothing counted yet: every field is still there
	CPPUNIT_ASSERT_EQUAL(std::string("R t=0 priv=0.0 ws=0.0 commit=0.0 handles=0 gdi=0 user=0 threads=0 tex=0 buf=0 xrsc=0"),
	                     platform::formatResources(platform::ResourceSample()));

}

void ProcessResourcesTest::testSampleSeesTheProcess() {
	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	const platform::ProcessResources now = platform::sampleProcessResources();
	CPPUNIT_ASSERT(now.privateMB > 1.0);
	CPPUNIT_ASSERT(now.workingSetMB > 1.0);
	// The whole system's commit charge holds this process's private bytes
	CPPUNIT_ASSERT(now.commitMB >= now.privateMB);
	CPPUNIT_ASSERT(now.handles > 10);
	CPPUNIT_ASSERT(now.threads >= 1);
	#endif
}

void ProcessResourcesTest::testSampleSeesALeak() {
	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	// What the channel's "leak 64" does to the game: 64 MiB allocated and touched, kept
	const platform::ProcessResources before = platform::sampleProcessResources();
	std::vector<char> leak(size_t(64) << 20, 1);
	const platform::ProcessResources after = platform::sampleProcessResources();
	CPPUNIT_ASSERT_MESSAGE(std::to_string(before.privateMB) + " -> " + std::to_string(after.privateMB),
	                       after.privateMB - before.privateMB >= 60.0);
	CPPUNIT_ASSERT_MESSAGE(std::to_string(before.workingSetMB) + " -> " + std::to_string(after.workingSetMB),
	                       after.workingSetMB - before.workingSetMB >= 60.0);
	CPPUNIT_ASSERT(leak[leak.size() - 1] == 1);
	#endif
}

void ProcessResourcesTest::testSampleSeesThreads() {
	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	const platform::ProcessResources before = platform::sampleProcessResources();
	std::mutex mutex;
	std::condition_variable wake;
	bool release = false;
	int parked = 0;
	std::vector<std::thread> threads;
	for(int i = 0; i < 4; i++) {
		threads.emplace_back([&] {
			std::unique_lock<std::mutex> lock(mutex);
			parked++;
			wake.notify_all();
			wake.wait(lock, [&] { return release; });
		});
	}
	{
		std::unique_lock<std::mutex> lock(mutex);
		wake.wait(lock, [&] { return parked == 4; });
	}
	const platform::ProcessResources during = platform::sampleProcessResources();
	{
		std::lock_guard<std::mutex> lock(mutex);
		release = true;
	}
	wake.notify_all();
	for(std::thread & thread : threads) {
		thread.join();
	}
	CPPUNIT_ASSERT_MESSAGE(std::to_string(before.threads) + " -> " + std::to_string(during.threads),
	                       during.threads >= before.threads + 4);
	#endif
}

void ProcessResourcesTest::testSampleSeesHandles() {
	#if ARX_PLATFORM == ARX_PLATFORM_WIN32
	const platform::ProcessResources before = platform::sampleProcessResources();
	std::vector<HANDLE> events;
	for(int i = 0; i < 64; i++) {
		events.push_back(CreateEventW(nullptr, TRUE, FALSE, nullptr));
	}
	const platform::ProcessResources during = platform::sampleProcessResources();
	for(HANDLE event : events) {
		if(event) {
			CloseHandle(event);
		}
	}
	CPPUNIT_ASSERT_MESSAGE(std::to_string(before.handles) + " -> " + std::to_string(during.handles),
	                       during.handles >= before.handles + 64);
	#endif
}
