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

#ifndef ARX_TESTS_PLATFORM_DIALOGPOLICYTEST_H
#define ARX_TESTS_PLATFORM_DIALOGPOLICYTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * Scripted and test launches never show a dialog (#501): which environment turns the dialogs off, the
 * one line written instead of a dialog, and the answer a dialog gets when it is not shown.
 */
class DialogPolicyTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(DialogPolicyTest);
	CPPUNIT_TEST(testEnvironment);
	CPPUNIT_TEST(testErrorLine);
	CPPUNIT_TEST(testMultiLineMessage);
	CPPUNIT_TEST(testQuestionsAnswerNo);
	CPPUNIT_TEST(testMessagesAnswerOk);
	CPPUNIT_TEST_SUITE_END();

public:

	void testEnvironment();
	void testErrorLine();
	void testMultiLineMessage();
	void testQuestionsAnswerNo();
	void testMessagesAnswerOk();

};

#endif // ARX_TESTS_PLATFORM_DIALOGPOLICYTEST_H
