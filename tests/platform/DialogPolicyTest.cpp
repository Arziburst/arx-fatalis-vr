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

#include "platform/DialogPolicyTest.h"

#include <string>

#include "platform/Dialog.h"

CppUnit::AutoRegisterSuite<DialogPolicyTest> g_registerDialogPolicyTest;

void DialogPolicyTest::testEnvironment() {
	// The owner's launches set neither: the dialogs are shown
	CPPUNIT_ASSERT(!platform::dialogsDisabledBy(nullptr, nullptr));
	CPPUNIT_ASSERT(!platform::dialogsDisabledBy("", ""));
	CPPUNIT_ASSERT(!platform::dialogsDisabledBy("0", nullptr));
	// Every scripted launch sets ARXVR_NO_DIALOGS=1
	CPPUNIT_ASSERT(platform::dialogsDisabledBy("1", nullptr));
	CPPUNIT_ASSERT(platform::dialogsDisabledBy("yes", nullptr));
	// The test channel (--test-dir) sets ARX_UNATTENDED=1: no dialog either
	CPPUNIT_ASSERT(platform::dialogsDisabledBy(nullptr, "1"));
	CPPUNIT_ASSERT(platform::dialogsDisabledBy("0", "1"));
	// Any non-empty ARX_UNATTENDED counts, as the crash processor has always read it
	CPPUNIT_ASSERT(platform::dialogsDisabledBy(nullptr, "0"));
}

void DialogPolicyTest::testErrorLine() {
	// The command line parse error of `arx --no-such-option`
	std::string line = platform::formatDialogLine(platform::DialogError, "Critical Error - Arx Fatalis VR",
	                                              "unknown option: --no-such-option");
	CPPUNIT_ASSERT_EQUAL(std::string("[dialog] error: Critical Error - Arx Fatalis VR: unknown option: --no-such-option"),
	                     line);
}

void DialogPolicyTest::testMultiLineMessage() {
	// CriticalErrorDialog's message: the error, a blank line, the log hint - one line in the log
	std::string line = platform::formatDialogLine(platform::DialogError, "Critical Error - Arx Fatalis VR",
	                                              "\nCould not load the data.\r\n\r\nYou might want to look at the log"
	                                              " for more details:\nC:\\arx.log\n");
	CPPUNIT_ASSERT_EQUAL(std::string("[dialog] error: Critical Error - Arx Fatalis VR: Could not load the data. |"
	                                 " You might want to look at the log for more details: | C:\\arx.log"), line);
	CPPUNIT_ASSERT(line.find('\n') == std::string::npos);
	CPPUNIT_ASSERT(line.find('\r') == std::string::npos);
}

void DialogPolicyTest::testQuestionsAnswerNo() {
	// A question gets the answer of closing it: nothing it offers (the data installer) is run unasked
	CPPUNIT_ASSERT(!platform::dialogDefaultAnswer(platform::DialogYesNo));
	CPPUNIT_ASSERT(!platform::dialogDefaultAnswer(platform::DialogWarnYesNo));
	CPPUNIT_ASSERT(!platform::dialogDefaultAnswer(platform::DialogOkCancel));
	CPPUNIT_ASSERT_EQUAL(std::string("[dialog] warn-yes-no: Error - Arx: No data. | -> Install it? [answered: no]"),
	                     platform::formatDialogLine(platform::DialogWarnYesNo, "Error - Arx", "No data.\n\n-> Install it?"));
	CPPUNIT_ASSERT_EQUAL(std::string("[dialog] yes-no: Arx: Install it? [answered: no]"),
	                     platform::formatDialogLine(platform::DialogYesNo, "Arx", "Install it?"));
	CPPUNIT_ASSERT_EQUAL(std::string("[dialog] ok-cancel: Arx: Go on? [answered: cancel]"),
	                     platform::formatDialogLine(platform::DialogOkCancel, "Arx", "Go on?"));
}

void DialogPolicyTest::testMessagesAnswerOk() {
	CPPUNIT_ASSERT(platform::dialogDefaultAnswer(platform::DialogInfo));
	CPPUNIT_ASSERT(platform::dialogDefaultAnswer(platform::DialogWarning));
	CPPUNIT_ASSERT(platform::dialogDefaultAnswer(platform::DialogError));
	CPPUNIT_ASSERT_EQUAL(std::string("[dialog] info: Information: Done"),
	                     platform::formatDialogLine(platform::DialogInfo, "Information", "Done"));
	CPPUNIT_ASSERT_EQUAL(std::string("[dialog] warning: Warning: Careful"),
	                     platform::formatDialogLine(platform::DialogWarning, "Warning", "Careful"));
}
