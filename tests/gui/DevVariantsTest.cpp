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

#include "gui/DevVariantsTest.h"

#include <cmath>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "gui/DevVariants.h"
#include "io/log/LogBackend.h"
#include "io/log/Logger.h"

CppUnit::AutoRegisterSuite<DevVariantsTest> g_registerDevVariantsTest;

namespace {

//! Keeps every logged message that starts with "[dev] variant" (the switch lines)
class DvLogCapture final : public logger::Backend {

public:

	std::vector<std::string> lines;

	void log(const logger::Source & /* file */, int /* line */, Logger::LogLevel /* level */,
	         std::string_view str) override {
		if(str.substr(0, 13) == "[dev] variant") {
			lines.emplace_back(str);
		}
	}

};

DvLogCapture * g_dvLog = nullptr;

std::vector<size_t> g_dvChosen;
std::vector<float> g_dvNumbers;

void dvChoiceSetter(size_t index) {
	g_dvChosen.push_back(index);
}

void dvNumberSetter(float value) {
	g_dvNumbers.push_back(value);
}

void dvNear(const std::string & what, float expected, float actual, float tolerance = 0.0001f) {
	if(std::abs(expected - actual) > tolerance) {
		CPPUNIT_FAIL(what + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

void dvEqual(const std::string & what, const std::string & expected, const std::string & actual) {
	if(expected != actual) {
		CPPUNIT_FAIL(what + ": expected \"" + expected + "\", got \"" + actual + "\"");
	}
}

void dvCount(const std::string & what, size_t expected, size_t actual) {
	if(expected != actual) {
		CPPUNIT_FAIL(what + ": expected " + std::to_string(expected) + ", got " + std::to_string(actual));
	}
}

std::string dvRect(const Rectf & r) {
	return "(" + std::to_string(r.left) + ", " + std::to_string(r.top) + ")-(" + std::to_string(r.right) + ", "
	       + std::to_string(r.bottom) + ")";
}

//! The font of the panel's own example: three named choices of PR #600
void dvRegisterFont(std::string_view pr = "#600") {
	CPPUNIT_ASSERT(devvariants::registerChoice("font", pr, { "Font 1", "Font 2", "Font 3" }, 0, dvChoiceSetter));
}

//! A threshold 0.3 .. 0.7 in steps of 0.1 (the owner's "Threshold 0.3/0.5/0.7" as a number)
void dvRegisterThreshold(std::string_view pr = "#600") {
	CPPUNIT_ASSERT(devvariants::registerNumber("threshold", pr, 0.3f, 0.7f, 0.1f, "", 0.5f, dvNumberSetter));
}

/*!
 * The rules every page of the panel keeps: every rect inside the area, none overlapping another, readable sizes,
 * unique channel targets, a block's PR is its groups' PR
 */
void dvCheckPage(const std::string & what, const devvariants::Layout & layout, const Rectf & area, float rowHeight) {

	struct Named { std::string name; Rectf rect; bool button; };
	std::vector<Named> rects;
	for(size_t i = 0; i < layout.blocks.size(); i++) {
		rects.push_back({ "title of " + layout.blocks[i].prLabel, layout.blocks[i].title, false });
	}
	for(const devvariants::GroupLayout & group : layout.groups) {
		rects.push_back({ "label of " + group.group, group.label, false });
		if(!group.value.empty()) {
			rects.push_back({ "value of " + group.group, group.value, false });
		}
		if(group.block >= layout.blocks.size()) {
			CPPUNIT_FAIL(what + ": group " + group.group + " is in block " + std::to_string(group.block) + " of "
			             + std::to_string(layout.blocks.size()));
		}
		const devvariants::Entry * entry = devvariants::find(group.group);
		if(!entry) {
			CPPUNIT_FAIL(what + ": the page shows an unregistered group " + group.group);
		}
		dvEqual(what + ": the block of " + group.group, entry->prLabel, layout.blocks[group.block].prLabel);
	}
	std::set<std::string> targets;
	for(const devvariants::Button & button : layout.buttons) {
		rects.push_back({ "button " + button.target, button.rect, true });
		if(!targets.insert(button.target).second) {
			CPPUNIT_FAIL(what + ": two buttons are " + button.target);
		}
	}

	std::set<std::string> blockLabels;
	for(const devvariants::BlockLayout & block : layout.blocks) {
		if(!blockLabels.insert(block.prLabel).second) {
			CPPUNIT_FAIL(what + ": two blocks for " + block.prLabel + " on one page");
		}
	}

	for(size_t i = 0; i < rects.size(); i++) {
		const Named & a = rects[i];
		if(!a.rect.isValid() || !area.contains(a.rect)) {
			CPPUNIT_FAIL(what + ": " + a.name + " " + dvRect(a.rect) + " is not inside the area " + dvRect(area));
		}
		if(a.rect.height() < 0.8f * rowHeight - 0.001f) {
			CPPUNIT_FAIL(what + ": " + a.name + " is " + std::to_string(a.rect.height()) + " high, rows are "
			             + std::to_string(rowHeight));
		}
		if(a.button && a.rect.width() < rowHeight - 0.001f) {
			CPPUNIT_FAIL(what + ": " + a.name + " is " + std::to_string(a.rect.width()) + " wide, less than a row's "
			             + std::to_string(rowHeight));
		}
		for(size_t j = i + 1; j < rects.size(); j++) {
			if(a.rect.overlaps(rects[j].rect)) {
				CPPUNIT_FAIL(what + ": " + a.name + " " + dvRect(a.rect) + " overlaps " + rects[j].name + " "
				             + dvRect(rects[j].rect));
			}
		}
	}
}

size_t dvButtons(const devvariants::Layout & layout, devvariants::ButtonKind kind) {
	size_t count = 0;
	for(const devvariants::Button & button : layout.buttons) {
		if(button.kind == kind) {
			count++;
		}
	}
	return count;
}

const devvariants::Button * dvButton(const devvariants::Layout & layout, const std::string & target) {
	for(const devvariants::Button & button : layout.buttons) {
		if(button.target == target) {
			return &button;
		}
	}
	return nullptr;
}

} // anonymous namespace

void DevVariantsTest::setUp() {
	devvariants::clear();
	g_dvChosen.clear();
	g_dvNumbers.clear();
	g_dvLog = new DvLogCapture;
	Logger::add(g_dvLog);
}

void DevVariantsTest::tearDown() {
	Logger::remove(g_dvLog);
	delete g_dvLog;
	g_dvLog = nullptr;
	devvariants::clear();
}

void DevVariantsTest::testRegisterAndListInOrder() {

	CPPUNIT_ASSERT(devvariants::entries().empty());

	dvRegisterFont("#600");
	dvRegisterThreshold("#601");
	CPPUNIT_ASSERT(devvariants::registerChoice("hilt", "#600", { "Low", "High" }, 1, dvChoiceSetter));

	const std::vector<devvariants::Entry> list = devvariants::entries();
	dvCount("groups", 3, list.size());
	dvEqual("first", "font", list[0].group);
	dvEqual("second", "threshold", list[1].group);
	dvEqual("third", "hilt", list[2].group);

	dvEqual("font's PR", "#600", list[0].prLabel);
	CPPUNIT_ASSERT(list[0].kind == devvariants::Kind::Choice);
	dvCount("font's choices", 3, list[0].choices.size());
	dvEqual("font's second choice", "Font 2", list[0].choices[1]);
	dvCount("font's default", 0, list[0].current);
	dvCount("hilt's default", 1, list[2].current);

	dvEqual("threshold's PR", "#601", list[1].prLabel);
	CPPUNIT_ASSERT(list[1].kind == devvariants::Kind::Number);
	dvNear("threshold min", 0.3f, list[1].min);
	dvNear("threshold max", 0.7f, list[1].max);
	dvNear("threshold step", 0.1f, list[1].step);
	dvNear("threshold default", 0.5f, list[1].value);

	CPPUNIT_ASSERT(devvariants::find("threshold") != nullptr);
	CPPUNIT_ASSERT(devvariants::find("nothing") == nullptr);
}

void DevVariantsTest::testRegisterRefusesBadGroups() {

	CPPUNIT_ASSERT(!devvariants::registerChoice("", "#600", { "A", "B" }, 0, dvChoiceSetter));
	CPPUNIT_ASSERT(!devvariants::registerChoice("font size", "#600", { "A", "B" }, 0, dvChoiceSetter));
	CPPUNIT_ASSERT(!devvariants::registerChoice("font", "#600", { }, 0, dvChoiceSetter));
	CPPUNIT_ASSERT(!devvariants::registerChoice("font", "#600", { "A", "B" }, 2, dvChoiceSetter));
	CPPUNIT_ASSERT(!devvariants::registerNumber("offset", "#600", 1.f, 1.f, 0.1f, "m", 1.f, dvNumberSetter));
	CPPUNIT_ASSERT(!devvariants::registerNumber("offset", "#600", 2.f, 1.f, 0.1f, "m", 1.f, dvNumberSetter));
	CPPUNIT_ASSERT(!devvariants::registerNumber("offset", "#600", 0.f, 1.f, 0.f, "m", 0.5f, dvNumberSetter));
	CPPUNIT_ASSERT(!devvariants::registerNumber("offset", "#600", 0.f, 1.f, -0.1f, "m", 0.5f, dvNumberSetter));
	CPPUNIT_ASSERT(!devvariants::registerNumber("off set", "#600", 0.f, 1.f, 0.1f, "m", 0.5f, dvNumberSetter));

	dvCount("nothing registered", 0, devvariants::entries().size());
}

void DevVariantsTest::testRegisterAgainReplaces() {

	dvRegisterFont();
	dvRegisterThreshold();
	// The same id again (a PR's line run twice, e.g. on every level load): replaced in its place, not added
	CPPUNIT_ASSERT(devvariants::registerChoice("font", "#600", { "Serif", "Sans" }, 1, dvChoiceSetter));

	const std::vector<devvariants::Entry> list = devvariants::entries();
	dvCount("groups", 2, list.size());
	dvEqual("still first", "font", list[0].group);
	dvCount("new choices", 2, list[0].choices.size());
	dvEqual("new choice", "Sans", list[0].choices[list[0].current]);
}

void DevVariantsTest::testRegisterCallsNoSetter() {

	dvRegisterFont();
	dvRegisterThreshold();
	dvCount("choice setter calls", 0, g_dvChosen.size());
	dvCount("number setter calls", 0, g_dvNumbers.size());
	dvCount("log lines", 0, g_dvLog->lines.size());
}

void DevVariantsTest::testChooseCallsTheSetterAtOnce() {

	dvRegisterFont();

	CPPUNIT_ASSERT(devvariants::choose("font", 2));
	// At once: before choose() returned
	dvCount("setter calls", 1, g_dvChosen.size());
	dvCount("the chosen index", 2, g_dvChosen[0]);
	dvCount("current", 2, devvariants::find("font")->current);

	CPPUNIT_ASSERT(devvariants::choose("font", 1));
	dvCount("setter calls", 2, g_dvChosen.size());
	dvCount("the chosen index", 1, g_dvChosen[1]);
	dvCount("current", 1, devvariants::find("font")->current);
}

void DevVariantsTest::testChooseSameOrInvalidDoesNothing() {

	dvRegisterFont();
	dvRegisterThreshold();

	CPPUNIT_ASSERT(!devvariants::choose("font", 0));        // the current one
	CPPUNIT_ASSERT(!devvariants::choose("font", 3));        // out of range
	CPPUNIT_ASSERT(!devvariants::choose("nothing", 1));     // unknown group
	CPPUNIT_ASSERT(!devvariants::choose("threshold", 1));   // a number has no choices
	CPPUNIT_ASSERT(!devvariants::step("font", 1));          // a choice has no step
	CPPUNIT_ASSERT(!devvariants::step("nothing", 1));

	dvCount("choice setter calls", 0, g_dvChosen.size());
	dvCount("number setter calls", 0, g_dvNumbers.size());
	dvCount("log lines", 0, g_dvLog->lines.size());
	dvCount("font current", 0, devvariants::find("font")->current);
	dvNear("threshold value", 0.5f, devvariants::find("threshold")->value);
}

void DevVariantsTest::testNumberStepsAndClamps() {

	CPPUNIT_ASSERT(devvariants::registerNumber("offset", "#600", 0.f, 1.f, 0.25f, "m", 0.5f, dvNumberSetter));

	CPPUNIT_ASSERT(devvariants::step("offset", 1));
	dvNear("+", 0.75f, devvariants::find("offset")->value);
	dvCount("setter calls", 1, g_dvNumbers.size());
	dvNear("setter got", 0.75f, g_dvNumbers.back());

	CPPUNIT_ASSERT(devvariants::step("offset", 1));
	dvNear("+ to the max", 1.f, devvariants::find("offset")->value);
	dvNear("setter got", 1.f, g_dvNumbers.back());

	// + at the max: clamped, nothing called, nothing logged
	CPPUNIT_ASSERT(!devvariants::step("offset", 1));
	dvNear("+ at the max", 1.f, devvariants::find("offset")->value);
	dvCount("setter calls", 2, g_dvNumbers.size());
	dvCount("log lines", 2, g_dvLog->lines.size());

	for(int i = 0; i < 4; i++) {
		CPPUNIT_ASSERT(devvariants::step("offset", -1));
	}
	dvNear("- to the min", 0.f, devvariants::find("offset")->value);
	dvNear("setter got", 0.f, g_dvNumbers.back());
	dvCount("setter calls", 6, g_dvNumbers.size());

	CPPUNIT_ASSERT(!devvariants::step("offset", -1));
	dvNear("- at the min", 0.f, devvariants::find("offset")->value);
	dvCount("setter calls", 6, g_dvNumbers.size());
	dvCount("log lines", 6, g_dvLog->lines.size());
}

void DevVariantsTest::testNumberStepsWithoutDrift() {

	// 0.1 is no exact float: three steps from 0 must read 0.3, and the max 0.5 must be reached and held
	CPPUNIT_ASSERT(devvariants::registerNumber("threshold", "#600", 0.f, 0.5f, 0.1f, "", 0.f, dvNumberSetter));
	for(int i = 0; i < 3; i++) {
		CPPUNIT_ASSERT(devvariants::step("threshold", 1));
	}
	dvEqual("three steps of 0.1", "0.3", devvariants::valueText("threshold"));
	dvNear("three steps of 0.1", 0.3f, devvariants::find("threshold")->value, 0.00001f);
	CPPUNIT_ASSERT(devvariants::step("threshold", 1));
	CPPUNIT_ASSERT(devvariants::step("threshold", 1));
	dvEqual("the max", "0.5", devvariants::valueText("threshold"));
	CPPUNIT_ASSERT(!devvariants::step("threshold", 1));
	dvCount("setter calls", 5, g_dvNumbers.size());
	dvNear("setter got the max", 0.5f, g_dvNumbers.back(), 0.00001f);
}

void DevVariantsTest::testNumberDefaultClamped() {

	CPPUNIT_ASSERT(devvariants::registerNumber("high", "#600", 0.f, 1.f, 0.25f, "", 5.f, dvNumberSetter));
	CPPUNIT_ASSERT(devvariants::registerNumber("low", "#600", 0.f, 1.f, 0.25f, "", -5.f, dvNumberSetter));
	dvNear("above the max", 1.f, devvariants::find("high")->value);
	dvNear("below the min", 0.f, devvariants::find("low")->value);
	dvCount("setter calls", 0, g_dvNumbers.size());
}

void DevVariantsTest::testSetByLabelAndNumber() {

	dvRegisterFont();
	CPPUNIT_ASSERT(devvariants::registerNumber("offset", "#600", 0.f, 1.f, 0.25f, "m", 0.5f, dvNumberSetter));

	// A choice by its exact label (the channel's devvariants set; labels may hold spaces)
	CPPUNIT_ASSERT(devvariants::set("font", "Font 3"));
	dvCount("font current", 2, devvariants::find("font")->current);
	dvCount("choice setter calls", 1, g_dvChosen.size());
	dvCount("the chosen index", 2, g_dvChosen.back());
	CPPUNIT_ASSERT(!devvariants::set("font", "Font 9"));
	CPPUNIT_ASSERT(!devvariants::set("font", "Font 3"));     // already
	dvCount("choice setter calls", 1, g_dvChosen.size());

	// A number, clamped
	CPPUNIT_ASSERT(devvariants::set("offset", "0.25"));
	dvNear("set", 0.25f, devvariants::find("offset")->value);
	dvNear("setter got", 0.25f, g_dvNumbers.back());
	CPPUNIT_ASSERT(devvariants::set("offset", "7"));
	dvNear("set above the max", 1.f, devvariants::find("offset")->value);
	dvNear("setter got", 1.f, g_dvNumbers.back());
	CPPUNIT_ASSERT(!devvariants::set("offset", "high"));
	CPPUNIT_ASSERT(!devvariants::set("nothing", "1"));
	dvCount("number setter calls", 2, g_dvNumbers.size());
	dvNear("unchanged", 1.f, devvariants::find("offset")->value);
}

void DevVariantsTest::testLogLine() {

	dvRegisterFont();
	CPPUNIT_ASSERT(devvariants::registerNumber("offset", "#600", 0.f, 1.f, 0.25f, "m", 0.5f, dvNumberSetter));
	CPPUNIT_ASSERT(devvariants::registerNumber("count", "#600", 0.f, 10.f, 1.f, "", 3.f, dvNumberSetter));

	CPPUNIT_ASSERT(devvariants::choose("font", 1));
	CPPUNIT_ASSERT(devvariants::step("offset", 1));
	CPPUNIT_ASSERT(devvariants::step("count", -1));
	CPPUNIT_ASSERT(devvariants::set("offset", "1"));

	dvCount("one line per switch", 4, g_dvLog->lines.size());
	dvEqual("a choice", "[dev] variant font = Font 2", g_dvLog->lines[0]);
	dvEqual("a number with a unit", "[dev] variant offset = 0.75 m", g_dvLog->lines[1]);
	dvEqual("a number without a unit", "[dev] variant count = 2", g_dvLog->lines[2]);
	dvEqual("the channel's set", "[dev] variant offset = 1 m", g_dvLog->lines[3]);
}

void DevVariantsTest::testValueText() {

	dvRegisterFont();
	CPPUNIT_ASSERT(devvariants::registerNumber("offset", "#600", 0.f, 1.f, 0.25f, "m", 0.75f, dvNumberSetter));
	CPPUNIT_ASSERT(devvariants::registerNumber("scale", "#600", 0.f, 200.f, 5.f, "%", 100.f, dvNumberSetter));
	CPPUNIT_ASSERT(devvariants::registerNumber("fine", "#600", 0.f, 1.f, 0.001f, "", 0.125f, dvNumberSetter));

	dvEqual("a choice", "Font 1", devvariants::valueText("font"));
	dvEqual("decimals", "0.75 m", devvariants::valueText("offset"));
	dvEqual("a whole number", "100 %", devvariants::valueText("scale"));
	dvEqual("three decimals", "0.125", devvariants::valueText("fine"));
	dvEqual("unknown", "", devvariants::valueText("nothing"));
}

void DevVariantsTest::testUnregister() {

	dvRegisterFont();
	dvRegisterThreshold();
	CPPUNIT_ASSERT(devvariants::registerChoice("hilt", "#601", { "Low", "High" }, 0, dvChoiceSetter));

	CPPUNIT_ASSERT(devvariants::unregister("threshold"));
	CPPUNIT_ASSERT(!devvariants::unregister("threshold"));
	CPPUNIT_ASSERT(!devvariants::unregister("nothing"));

	const std::vector<devvariants::Entry> list = devvariants::entries();
	dvCount("groups", 2, list.size());
	dvEqual("first", "font", list[0].group);
	dvEqual("second", "hilt", list[1].group);
	CPPUNIT_ASSERT(devvariants::find("threshold") == nullptr);
	CPPUNIT_ASSERT(!devvariants::step("threshold", 1));
	dvCount("no setter after unregistering", 0, g_dvNumbers.size());

	devvariants::clear();
	CPPUNIT_ASSERT(devvariants::entries().empty());
}

void DevVariantsTest::testAfterRestartKept() {

	CPPUNIT_ASSERT(devvariants::registerChoice("textures", "#600", { "Low", "High" }, 0, dvChoiceSetter, true));
	dvRegisterFont();

	CPPUNIT_ASSERT(devvariants::find("textures")->afterRestart);
	CPPUNIT_ASSERT(!devvariants::find("font")->afterRestart);

	const devvariants::Layout layout = devvariants::layout(Rectf(0.f, 0.f, 800.f, 600.f), 24.f, 0);
	dvCount("groups on the page", 2, layout.groups.size());
	for(const devvariants::GroupLayout & group : layout.groups) {
		CPPUNIT_ASSERT_EQUAL(group.group == "textures", group.afterRestart);
	}
}

void DevVariantsTest::testMapPageView() {

	using devvariants::MapPageView;
	using devvariants::mapPageView;

	// Not a development launch: always the map - players never see the panel, whatever is registered
	CPPUNIT_ASSERT(mapPageView(false, 0, false) == MapPageView::Map);
	CPPUNIT_ASSERT(mapPageView(false, 3, false) == MapPageView::Map);
	CPPUNIT_ASSERT(mapPageView(false, 0, true) == MapPageView::Map);
	CPPUNIT_ASSERT(mapPageView(false, 3, true) == MapPageView::Map);

	// A development launch: the panel with groups; without, the notice and Show map, then the map
	CPPUNIT_ASSERT(mapPageView(true, 2, false) == MapPageView::Panel);
	CPPUNIT_ASSERT(mapPageView(true, 2, true) == MapPageView::Panel);
	CPPUNIT_ASSERT(mapPageView(true, 0, false) == MapPageView::Empty);
	CPPUNIT_ASSERT(mapPageView(true, 0, true) == MapPageView::Map);
}

void DevVariantsTest::testLayoutEmpty() {

	const Rectf area(100.f, 50.f, 500.f, 450.f);
	const devvariants::Layout layout = devvariants::layout(area, 24.f, 0);

	CPPUNIT_ASSERT(layout.empty);
	dvEqual("notice", "No variants under test", layout.notice);
	dvCount("pages", 1, layout.pages);
	dvCount("page", 0, layout.page);
	dvCount("blocks", 0, layout.blocks.size());
	dvCount("groups", 0, layout.groups.size());
	dvCount("buttons", 1, layout.buttons.size());
	CPPUNIT_ASSERT(layout.buttons[0].kind == devvariants::ButtonKind::ShowMap);
	dvEqual("target", "devpanel.show_map", layout.buttons[0].target);
	dvCheckPage("empty", layout, area, 24.f);
}

void DevVariantsTest::testLayoutGroups() {

	dvRegisterFont();
	CPPUNIT_ASSERT(devvariants::registerNumber("offset", "#600", 0.f, 1.f, 0.25f, "m", 0.5f, dvNumberSetter));

	const Rectf area(100.f, 50.f, 600.f, 450.f);
	const devvariants::Layout layout = devvariants::layout(area, 24.f, 0);

	CPPUNIT_ASSERT(!layout.empty);
	dvCount("pages", 1, layout.pages);
	dvCount("blocks", 1, layout.blocks.size());
	dvEqual("the block's PR", "#600", layout.blocks[0].prLabel);
	dvCount("groups", 2, layout.groups.size());
	dvEqual("first group", "font", layout.groups[0].group);
	dvEqual("second group", "offset", layout.groups[1].group);
	dvEqual("the number's text", "0.5 m", layout.groups[1].valueText);
	dvEqual("the choice's text", "Font 1", layout.groups[0].valueText);
	CPPUNIT_ASSERT(layout.groups[0].value.empty());

	dvCount("choice buttons", 3, dvButtons(layout, devvariants::ButtonKind::Choice));
	dvCount("minus", 1, dvButtons(layout, devvariants::ButtonKind::Minus));
	dvCount("plus", 1, dvButtons(layout, devvariants::ButtonKind::Plus));
	dvCount("no paging", 0, dvButtons(layout, devvariants::ButtonKind::PrevPage)
	                        + dvButtons(layout, devvariants::ButtonKind::NextPage));
	dvCount("no show map", 0, dvButtons(layout, devvariants::ButtonKind::ShowMap));
	dvCount("buttons", 5, layout.buttons.size());

	for(size_t i = 0; i < 3; i++) {
		const devvariants::Button * button = dvButton(layout, "devpanel.font." + std::to_string(i));
		if(!button) {
			CPPUNIT_FAIL("no button devpanel.font." + std::to_string(i));
		}
		CPPUNIT_ASSERT(button->kind == devvariants::ButtonKind::Choice);
		dvEqual("its group", "font", button->group);
		dvCount("its index", i, button->index);
		dvEqual("its text", "Font " + std::to_string(i + 1), button->label);
		CPPUNIT_ASSERT_EQUAL(i == 0, button->current);
	}

	// "-  value  +": the value between the two buttons, on their row
	const devvariants::Button * minus = dvButton(layout, "devpanel.offset.minus");
	const devvariants::Button * plus = dvButton(layout, "devpanel.offset.plus");
	CPPUNIT_ASSERT(minus && plus);
	dvEqual("minus text", "-", minus->label);
	dvEqual("plus text", "+", plus->label);
	dvEqual("minus group", "offset", minus->group);
	const Rectf value = layout.groups[1].value;
	CPPUNIT_ASSERT(value.isValid());
	CPPUNIT_ASSERT(minus->rect.right <= value.left + 0.001f);
	CPPUNIT_ASSERT(value.right <= plus->rect.left + 0.001f);
	CPPUNIT_ASSERT(value.center().y > minus->rect.top && value.center().y < minus->rect.bottom);
	CPPUNIT_ASSERT(value.center().y > plus->rect.top && value.center().y < plus->rect.bottom);

	dvCheckPage("two groups", layout, area, 24.f);
}

void DevVariantsTest::testLayoutHighlightFollowsChoice() {

	dvRegisterFont();
	CPPUNIT_ASSERT(devvariants::choose("font", 2));

	const devvariants::Layout layout = devvariants::layout(Rectf(0.f, 0.f, 600.f, 400.f), 24.f, 0);
	for(size_t i = 0; i < 3; i++) {
		const devvariants::Button * button = dvButton(layout, "devpanel.font." + std::to_string(i));
		CPPUNIT_ASSERT(button);
		CPPUNIT_ASSERT_EQUAL(i == 2, button->current);
	}
	dvEqual("shown value", "Font 3", layout.groups[0].valueText);
}

void DevVariantsTest::testLayoutWideChoicesWrap() {

	// Eight long choices in a narrow page: they wrap inside it rather than run off its edge
	std::vector<std::string> choices;
	for(int i = 1; i <= 8; i++) {
		choices.push_back("Variant number " + std::to_string(i));
	}
	CPPUNIT_ASSERT(devvariants::registerChoice("wide", "#600", choices, 0, dvChoiceSetter));
	dvRegisterThreshold();

	const Rectf area(0.f, 0.f, 220.f, 600.f);
	size_t buttons = 0;
	const devvariants::Layout first = devvariants::layout(area, 20.f, 0);
	for(size_t page = 0; page < first.pages; page++) {
		const devvariants::Layout layout = devvariants::layout(area, 20.f, page);
		dvCheckPage("narrow page " + std::to_string(page), layout, area, 20.f);
		buttons += dvButtons(layout, devvariants::ButtonKind::Choice);
	}
	dvCount("every choice has its button", 8, buttons);
}

void DevVariantsTest::testLayoutBlocksPerPr() {

	// A combined test branch (arx-batch-headset-test): two PRs' groups registered interleaved
	CPPUNIT_ASSERT(devvariants::registerChoice("a1", "#601", { "One", "Two" }, 0, dvChoiceSetter));
	CPPUNIT_ASSERT(devvariants::registerNumber("b1", "#602", 0.f, 1.f, 0.1f, "", 0.5f, dvNumberSetter));
	CPPUNIT_ASSERT(devvariants::registerChoice("a2", "#601", { "One", "Two" }, 0, dvChoiceSetter));
	CPPUNIT_ASSERT(devvariants::registerChoice("b2", "#602", { "One", "Two", "Three" }, 0, dvChoiceSetter));

	const Rectf area(0.f, 0.f, 800.f, 700.f);
	const devvariants::Layout layout = devvariants::layout(area, 24.f, 0);
	dvCheckPage("two PRs", layout, area, 24.f);
	dvCount("pages", 1, layout.pages);
	dvCount("one block per PR", 2, layout.blocks.size());
	dvEqual("first block", "#601", layout.blocks[0].prLabel);
	dvEqual("second block", "#602", layout.blocks[1].prLabel);

	// The panel order: a PR's groups together, the PRs in the order their first group came
	dvCount("groups", 4, layout.groups.size());
	const char * const order[] = { "a1", "a2", "b1", "b2" };
	const size_t blocks[] = { 0, 0, 1, 1 };
	for(size_t i = 0; i < 4; i++) {
		dvEqual("group " + std::to_string(i), order[i], layout.groups[i].group);
		dvCount("block of " + std::string(order[i]), blocks[i], layout.groups[i].block);
	}
	// A block's title above its groups
	CPPUNIT_ASSERT(layout.blocks[0].title.bottom <= layout.groups[0].label.top + 0.001f);
	CPPUNIT_ASSERT(layout.blocks[1].title.bottom <= layout.groups[2].label.top + 0.001f);

	// The registry's own list stays in registration order
	const std::vector<devvariants::Entry> list = devvariants::entries();
	dvEqual("listed second", "b1", list[1].group);
}

void DevVariantsTest::testLayoutPagesWhenTooMany() {

	// 60 groups of two PRs, six choices each, interleaved: more than a 400 x 300 page can hold at rows of 24
	// (every group needs six buttons of at least 24 x 19.2 each: about 166 000 px2 of 120 000)
	std::vector<std::string> expected[2];
	for(int i = 0; i < 60; i++) {
		const std::string pr = (i % 2 == 0) ? "#601" : "#602";
		const std::string group = "g" + std::to_string(i);
		CPPUNIT_ASSERT(devvariants::registerChoice(group, pr, { "A", "B", "C", "D", "E", "F" }, 0, dvChoiceSetter));
		expected[i % 2].push_back(group);
	}
	std::vector<std::string> panelOrder = expected[0];
	panelOrder.insert(panelOrder.end(), expected[1].begin(), expected[1].end());

	const Rectf area(0.f, 0.f, 400.f, 300.f);
	const float row = 24.f;
	const devvariants::Layout first = devvariants::layout(area, row, 0);
	if(first.pages < 2) {
		CPPUNIT_FAIL("60 groups on a 400 x 300 page: expected more than one page, got " + std::to_string(first.pages));
	}

	std::vector<std::string> shown;
	for(size_t page = 0; page < first.pages; page++) {
		const devvariants::Layout layout = devvariants::layout(area, row, page);
		const std::string what = "page " + std::to_string(page);
		dvCount(what + ": page", page, layout.page);
		dvCount(what + ": pages", first.pages, layout.pages);
		CPPUNIT_ASSERT(!layout.empty);
		if(layout.groups.empty()) {
			CPPUNIT_FAIL(what + " shows no group");
		}
		dvCheckPage(what, layout, area, row);

		const bool hasPrev = dvButton(layout, "devpanel.prev") != nullptr;
		const bool hasNext = dvButton(layout, "devpanel.next") != nullptr;
		CPPUNIT_ASSERT_EQUAL(page > 0, hasPrev);
		CPPUNIT_ASSERT_EQUAL(page + 1 < first.pages, hasNext);
		dvCount(what + ": prev buttons", page > 0 ? 1 : 0, dvButtons(layout, devvariants::ButtonKind::PrevPage));
		dvCount(what + ": next buttons", page + 1 < first.pages ? 1 : 0,
		        dvButtons(layout, devvariants::ButtonKind::NextPage));

		for(const devvariants::GroupLayout & group : layout.groups) {
			shown.push_back(group.group);
			// The group whole on its page: all six choices
			for(size_t i = 0; i < 6; i++) {
				if(!dvButton(layout, "devpanel." + group.group + "." + std::to_string(i))) {
					CPPUNIT_FAIL(what + ": " + group.group + " is split, no choice " + std::to_string(i));
				}
			}
		}
	}

	// Every group exactly once over the pages, in the panel order
	dvCount("groups shown over the pages", panelOrder.size(), shown.size());
	for(size_t i = 0; i < panelOrder.size(); i++) {
		dvEqual("panel position " + std::to_string(i), panelOrder[i], shown[i]);
	}

	// A page past the last is the last
	const devvariants::Layout past = devvariants::layout(area, row, first.pages + 5);
	dvCount("past the last page", first.pages - 1, past.page);
}
