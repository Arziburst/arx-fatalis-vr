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

#ifndef ARX_TESTS_PLATFORM_PROCESSRESOURCESTEST_H
#define ARX_TESTS_PLATFORM_PROCESSRESOURCESTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The resource line of the performance log (#545): every 5 s arx-perf.log gets one "R" line with the process's
 * memory, handles and threads and the renderer's / OpenXR's live objects, so a leak or a handle storm shows as a
 * trend (scripts/dev/perf-summary.ps1, scripts/dev/test-resource-trend.ps1).
 *
 * The interface these tests expect - src/platform/ProcessResources.h, src/platform/ProcessResources.cpp (add the
 * .cpp to arxtest_SOURCES in tests/CMakeLists.txt):
 *
 *   namespace platform {
 *
 *   struct ProcessResources {
 *     double privateMB = 0.0;    //!< the process's private bytes (its commit charge), MiB
 *     double workingSetMB = 0.0; //!< the process's working set, MiB
 *     double commitMB = 0.0;     //!< the system's commit charge (all processes), MiB
 *     u32 handles = 0;           //!< kernel handles of the process
 *     u32 gdiObjects = 0;
 *     u32 userObjects = 0;
 *     u32 threads = 0;           //!< threads of the process
 *   };
 *
 *   //! The calling process now (cheap: called every 5 s off the frame path); zeros where a platform has no counter
 *   ProcessResources sampleProcessResources();
 *
 *   struct ResourceSample {
 *     u32 second = 0;            //!< since the performance log started, as the S lines' t
 *     ProcessResources process;
 *     u32 textures = 0;          //!< GL textures alive
 *     u32 buffers = 0;           //!< GL buffers alive
 *     u32 swapchains = 0;        //!< OpenXR swapchains alive
 *   };
 *
 *   //! "R t=35 priv=412.5 ws=380.0 commit=16402.3 handles=812 gdi=45 user=31 threads=42 tex=1830 buf=210 xrsc=6":
 *   //! MiB with one decimal (a '.' whatever the locale), counts as integers, always all ten fields in this order
 *   std::string formatResources(const ResourceSample & sample);
 *
 *   }
 */
class ProcessResourcesTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(ProcessResourcesTest);
	CPPUNIT_TEST(testLineFormat);
	CPPUNIT_TEST(testSampleSeesTheProcess);
	CPPUNIT_TEST(testSampleSeesALeak);
	CPPUNIT_TEST(testSampleSeesThreads);
	CPPUNIT_TEST(testSampleSeesHandles);
	CPPUNIT_TEST_SUITE_END();

public:

	void testLineFormat();
	void testSampleSeesTheProcess();
	void testSampleSeesALeak();
	void testSampleSeesThreads();
	void testSampleSeesHandles();

};

#endif // ARX_TESTS_PLATFORM_PROCESSRESOURCESTEST_H
