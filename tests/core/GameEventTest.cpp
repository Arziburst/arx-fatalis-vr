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

#include "core/GameEventTest.h"

#include <cmath>
#include <limits>
#include <string>

#include "core/GameEventFormat.h"

CppUnit::AutoRegisterSuite<GameEventTest> g_registerGameEventTest;

using gameevent::Limiter;
using gameevent::Line;

void GameEventTest::nameOnlyTest() {
	CPPUNIT_ASSERT_EQUAL(std::string("game.new"), Line("game.new").str());
}

void GameEventTest::numbersTest() {
	Line line("level.loaded");
	line.field("area", 10u).field("ms", -5ll).field("count", size_t(0));
	CPPUNIT_ASSERT_EQUAL(std::string("level.loaded area=10 ms=-5 count=0"), line.str());
}

void GameEventTest::floatsTest() {
	Line line("melee.hit");
	line.field("damage", 12.345f, 1).field("aim", 0.5f).field("x", 2.0)
	    .field("bad", std::nan("")).field("big", -std::numeric_limits<double>::infinity());
	CPPUNIT_ASSERT_EQUAL(std::string("melee.hit damage=12.3 aim=0.50 x=2.00 bad=nan big=-inf"), line.str());
}

void GameEventTest::booleansTest() {
	Line line("save");
	line.field("ok", true).field("quick", false);
	CPPUNIT_ASSERT_EQUAL(std::string("save ok=yes quick=no"), line.str());
}

void GameEventTest::plainStringsTest() {
	Line line("spell.cast");
	line.field("spell", "magic_missile").field("target", std::string("goblin_base_0012"))
	    .field("runes", std::string_view("aam+taar")).field("path", "/user/hand/left");
	CPPUNIT_ASSERT_EQUAL(std::string("spell.cast spell=magic_missile target=goblin_base_0012"
	                                 " runes=aam+taar path=/user/hand/left"), line.str());
}

void GameEventTest::quotedStringsTest() {
	Line line("vr.focus");
	line.field("title", "Arx Fatalis VR").field("empty", "").field("eq", "a=b")
	    .field("quote", "say \"hi\"").field("slash", "C:\\games").field("lines", "one\ntwo")
	    .field("null", static_cast<const char *>(nullptr));
	CPPUNIT_ASSERT_EQUAL(std::string("vr.focus title=\"Arx Fatalis VR\" empty=\"\" eq=\"a=b\""
	                                 " quote=\"say \\\"hi\\\"\" slash=\"C:\\\\games\" lines=\"one\\ntwo\""
	                                 " null=\"\""), line.str());
}

void GameEventTest::limiterTest() {

	Limiter limiter;

	CPPUNIT_ASSERT_EQUAL(Limiter::Log, limiter.count("script.error", 2));
	CPPUNIT_ASSERT_EQUAL(Limiter::Log, limiter.count("script.error", 2));
	CPPUNIT_ASSERT_EQUAL(Limiter::Notice, limiter.count("script.error", 2));
	CPPUNIT_ASSERT_EQUAL(Limiter::Drop, limiter.count("script.error", 2));
	CPPUNIT_ASSERT_EQUAL(Limiter::Drop, limiter.count("script.error", 2));

	// Another name has its own count
	CPPUNIT_ASSERT_EQUAL(Limiter::Log, limiter.count("item.use", 2));

	// Every event is counted, logged or not
	CPPUNIT_ASSERT_EQUAL(size_t(6), limiter.total());
	CPPUNIT_ASSERT_EQUAL(size_t(5), limiter.counts().find("script.error")->second);
	CPPUNIT_ASSERT_EQUAL(size_t(1), limiter.counts().find("item.use")->second);
	CPPUNIT_ASSERT_EQUAL(size_t(2), limiter.counts().size());
}

void GameEventTest::limiterUnlimitedTest() {
	Limiter limiter;
	for(int i = 0; i < 1000; i++) {
		CPPUNIT_ASSERT_EQUAL(Limiter::Log, limiter.count("game.state", 0));
	}
	CPPUNIT_ASSERT_EQUAL(size_t(1000), limiter.total());
}
