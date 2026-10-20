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

#ifndef ARX_TESTS_CORE_CHANNELWRITERTEST_H
#define ARX_TESTS_CORE_CHANNELWRITERTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * The test channel's status.json and ack.txt written off the frame (#553): the caller never waits for a reader that
 * holds status.json, the ack never overtakes its status, the newest status wins, nothing handed over is lost at stop.
 */
class ChannelWriterTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(ChannelWriterTest);
	CPPUNIT_TEST(testStatusThenAck);
	CPPUNIT_TEST(testHeldStatusDelaysTheAckNotTheCaller);
	CPPUNIT_TEST(testNewestStatusWins);
	CPPUNIT_TEST(testPatienceRunsOutAndTheStatusIsTriedAgain);
	CPPUNIT_TEST(testKeptStatusIsHandedOverByPoll);
	CPPUNIT_TEST(testStopWritesWhatIsLeft);
	CPPUNIT_TEST_SUITE_END();

public:

	void testStatusThenAck();
	void testHeldStatusDelaysTheAckNotTheCaller();
	void testNewestStatusWins();
	void testPatienceRunsOutAndTheStatusIsTriedAgain();
	void testKeptStatusIsHandedOverByPoll();
	void testStopWritesWhatIsLeft();

};

#endif // ARX_TESTS_CORE_CHANNELWRITERTEST_H
