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

#ifndef ARX_TESTS_VR_FRAMEPACINGTEST_H
#define ARX_TESTS_VR_FRAMEPACINGTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

#include "vr/FramePacing.h"

/*!
 * The rate / cadence verdict of the VR frame loop (#20) decides from timestamps alone, so every
 * verdict is reproduced here from synthetic frame timings: a healthy loop, the even half-rate cap,
 * an uneven cadence that would ghost, an unpaced loop, a late run, a frame gap and its phase.
 */
class FramePacingTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(FramePacingTest);
	CPPUNIT_TEST(testMatchedEvenAtTheNativeRate);
	CPPUNIT_TEST(testHalfRateWithTheCapIsMatchedAndEven);
	CPPUNIT_TEST(testHalfRateWithoutTheCapIsUnderSubmitting);
	CPPUNIT_TEST(testJitterIsUnevenEvenWhenMatched);
	CPPUNIT_TEST(testAlternatingSlotsIsUneven);
	CPPUNIT_TEST(testUnpacedLoopIsOverSubmitting);
	CPPUNIT_TEST(testWorstFrameAndWorstRun);
	CPPUNIT_TEST(testFrameGapNamesThePhase);
	CPPUNIT_TEST(testReportsComeOncePerWindow);
	CPPUNIT_TEST(testFlushClosesAShortWindow);
	CPPUNIT_TEST(testFormatNamesTheVerdicts);
	CPPUNIT_TEST_SUITE_END();

	//! A synthetic frame loop: builds consecutive frame timings
	struct Loop {
		s64 period = 12500;    //!< one display slot in microseconds (80 Hz)
		s64 lastWaitEnd = 0;
		s64 lastEndEnd = 0;
		bool first = true;

		/*!
		 * The next frame, starting \a interval after the previous one: \a wait inside xrWaitFrame,
		 * \a work until xrEndFrame, \a end inside xrEndFrame (all microseconds)
		 */
		vr::FrameTiming frame(s64 interval, s64 wait = 11000, s64 work = 500, s64 end = 500);
	};

	//! Feed \a count frames of \a interval and return the number of reports that came out
	static unsigned feed(vr::PacingWindow & window, Loop & loop, unsigned count, s64 interval,
	                     s64 wait = 11000, s64 work = 500, s64 end = 500);

public:

	void testMatchedEvenAtTheNativeRate();
	void testHalfRateWithTheCapIsMatchedAndEven();
	void testHalfRateWithoutTheCapIsUnderSubmitting();
	void testJitterIsUnevenEvenWhenMatched();
	void testAlternatingSlotsIsUneven();
	void testUnpacedLoopIsOverSubmitting();
	void testWorstFrameAndWorstRun();
	void testFrameGapNamesThePhase();
	void testReportsComeOncePerWindow();
	void testFlushClosesAShortWindow();
	void testFormatNamesTheVerdicts();

};

#endif // ARX_TESTS_VR_FRAMEPACINGTEST_H
