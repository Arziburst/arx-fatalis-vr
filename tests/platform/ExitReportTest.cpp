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

#include "platform/ExitReportTest.h"

#include <limits>
#include <memory>
#include <string>

#include "platform/ExitReport.h"

CppUnit::AutoRegisterSuite<ExitReportTest> g_registerExitReportTest;

namespace {

bool reportContains(const platform::ExitReport & report, const std::string & part) {
	return std::string(report.text()).find(part) != std::string::npos;
}

} // anonymous namespace

void ExitReportTest::testNumbers() {
	platform::ExitReport report;
	report.appendNumber(0);
	report.append(" ");
	report.appendNumber(-1);
	report.append(" ");
	report.appendNumber(1234567);
	report.append(" ");
	report.appendNumber(std::numeric_limits<s64>::min());
	report.append(" ");
	report.appendHex(0);
	report.append(" ");
	report.appendHex(0xffffffffu);
	CPPUNIT_ASSERT_EQUAL(std::string("0 -1 1234567 -9223372036854775808 0x0 0xffffffff"), std::string(report.text()));
}

void ExitReportTest::testReportNamesCodeCauseAndStack() {
	// The shape of #146: a library calls ExitProcess(-1) a few seconds after the VR session ended
	const platform::ExitFrame frames[] = { { "ntdll.dll", 0x1234 }, { "vrclient_x64.dll", 0xabc }, { "", 0x10 } };
	platform::ExitFacts facts;
	facts.via = "ExitProcess";
	facts.exitCodeKnown = true;
	facts.exitCode = -1;
	facts.cause = "VR session ended by the runtime (exiting)";
	facts.causeAgoMs = 3200;
	facts.frames = frames;
	facts.frameCount = 3;
	platform::ExitReport report;
	platform::describeExit(report, facts);
	CPPUNIT_ASSERT(reportContains(report, "outside the game's own shutdown (#146): ExitProcess, exit code -1 (0xffffffff)\n"));
	CPPUNIT_ASSERT(reportContains(report, "Last cause noted before it: VR session ended by the runtime (exiting), 3200 ms earlier\n"));
	CPPUNIT_ASSERT(reportContains(report, "Called from (innermost first): ntdll.dll+0x1234 < vrclient_x64.dll+0xabc < ?+0x10\n"));
}

void ExitReportTest::testReportWithoutCauseOrStack() {
	platform::ExitFacts facts;
	facts.via = "exit()";
	platform::ExitReport report;
	platform::describeExit(report, facts);
	CPPUNIT_ASSERT(reportContains(report, "exit(), exit code not known yet\n"));
	CPPUNIT_ASSERT(reportContains(report, "Last cause noted before it: none"));
	CPPUNIT_ASSERT(reportContains(report, "Called from (innermost first): no stack\n"));
}

void ExitReportTest::testLinesAreErrorLines() {
	// Every line is an [E] line in arx.log's format: the smoke test and the log digest flag it
	platform::ExitFacts facts;
	facts.via = "exit()";
	platform::ExitReport report;
	platform::describeExit(report, facts);
	std::string text(report.text());
	size_t lines = 0;
	for(size_t start = 0; start < text.size(); lines++) {
		CPPUNIT_ASSERT_EQUAL(start, text.find("[E] ExitTrace:0  ", start));
		size_t end = text.find('\n', start);
		CPPUNIT_ASSERT(end != std::string::npos);
		start = end + 1;
	}
	CPPUNIT_ASSERT_EQUAL(size_t(3), lines);
}

void ExitReportTest::testOverflowIsCut() {
	auto report = std::make_unique<platform::ExitReport>();
	std::string chunk(1000, 'a');
	for(int i = 0; i < 10; i++) {
		report->append(chunk);
	}
	report->append("never");
	report->appendNumber(42);
	CPPUNIT_ASSERT_EQUAL(platform::ExitReport::Capacity, report->text().size());
	CPPUNIT_ASSERT_EQUAL(std::string("..."), std::string(report->text().substr(report->text().size() - 3)));
	CPPUNIT_ASSERT(!reportContains(*report, "never"));
}
