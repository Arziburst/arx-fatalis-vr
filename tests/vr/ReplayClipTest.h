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

#ifndef ARX_TESTS_VR_REPLAYCLIPTEST_H
#define ARX_TESTS_VR_REPLAYCLIPTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * Record and replay (#550): every VR session is written to arx-replay.log, a clip in the .vrsim dialect of the
 * simulated headset (src/vr/SimRig.h), and the simulator streams such a clip back into a fresh run.
 *
 * The interface these tests expect - src/vr/ReplayClip.h, src/vr/ReplayClip.cpp (add the .cpp to arxtest_SOURCES in
 * tests/CMakeLists.txt and to VR_SOURCES), plus these additions to src/vr/SimRig.h:
 *
 *   namespace vr {
 *
 *   // SimRig.h: the player in the engine as a clip recorded him - what the replay's drift compares; the simulated
 *   // runtime itself ignores it
 *   struct SimPlayer {
 *     Vec3f position = Vec3f(0.f); //!< engine units (1 m = 100, vr/WorldScale.h)
 *     float yaw = 0.f;             //!< degrees, as player.angle.getYaw()
 *     s32 area = -1;               //!< the level (g_currentArea), -1 = none (a menu)
 *   };
 *   struct SimRig {
 *     ...                                   // as today
 *     std::array<SimPose, 2> aims;          //!< NEW: each hand's aim pose in its grip's space (default: the grip itself,
 *                                           //!< the simulator's behaviour so far); the aim action space is located there
 *     SimPlayer player;                     //!< NEW: the `player` command
 *   };
 *   struct SimView {
 *     ...
 *     float floor = 1.7f;                   //!< NEW: meters from the LOCAL origin down to the floor (the STAGE space;
 *                                           //!< replaces VRSimulator.cpp's FloorBelowHead): `floor <m>`
 *   };
 *
 *   New script commands (Simulation::parse, so a clip loads as a script and the channel's vr-sim takes them too):
 *     head <x> <y> <z> <yaw> <pitch> <roll>                        the head pose in one command
 *     hand left|right <x> <y> <z> <yaw> <pitch> <roll>             the grip pose in one command
 *     hand left|right aim <x> <y> <z> <yaw> <pitch> <roll>         the aim pose in the grip's space
 *     player <x> <y> <z> <yaw> <area>                              SimRig::player (recorded, not simulated)
 *     floor <meters>                                               SimView::floor
 *     anchor level <area> | anchor save                            a clip's anchor: the clip player waits there; in a
 *                                                                  script loaded with --vr-sim it does nothing
 *     a line may hold several commands separated by ';' - "@<t>ms <command>; <command>; ..." runs them all at t
 *   and in the simulated runtime (VRSimulator, the channel's vr-sim and a --vr-sim script line):
 *     play <file> [from <ms>] [to <ms>]       stream a clip (ClipPlayer below); <file> absolute or relative to the test
 *                                             channel directory, in double quotes when it holds a space; a script's
 *                                             play starts at its timeline's frame 0, the channel's at the next frame
 *     play stop                               end it (done)
 *
 *   // ReplayClip.h
 *
 *   //! A clip's header: comment lines, then the set-up lines, before the first frame line
 *   struct ReplayHeader {
 *     std::string build;    //!< "# build: <text>"   arx_name + " " + arx_version
 *     std::string save;     //!< "# save: <text>"    "<save folder> <save name>" of the save made at the record start
 *                           //!<                     (e.g. "save0003 replay 2026-10-20 14-03-12"), or "none"
 *     u64 seed = 0;         //!< "# seed: <decimal>" the main thread's RNG seed (--seed <n>, else Random::currentSeed())
 *     float rate = 0.f;     //!< "# rate: <hz>"      the display rate at the record start
 *     std::string mark;     //!< "# mark: <name> <ms>" only in a clip cut at a `replay mark` ...
 *     s64 markMs = -1;      //!< ... the mark's display time in ms on the clip's clock
 *     float ipd = 0.f;      //!< set-up line "ipd <m>"   (0: not written)
 *     float floor = 0.f;    //!< set-up line "floor <m>" (0: not written)
 *   };
 *
 *   //! "# arx-replay ..." first, then "# build: ", "# save: ", "# seed: ", "# rate: " [, "# mark: "] and the set-up lines
 *   std::string formatReplayHeader(const ReplayHeader & header);
 *
 *   //! One frame of the stream: the rig the game saw at a predicted display time, and the player in the engine
 *   struct ReplayFrame {
 *     u64 timeUs = 0;       //!< predicted display time since the record start, microseconds
 *     SimRig rig;           //!< head, grips (hands) + handTracked, aims, fingers (five curls, joints on/off), profile,
 *                           //!< actions (the raw values the game read), focused / worn (the session), player
 *   };
 *
 *   //! One line "@<ms>ms <command>; <command>; ...\n" - ms = timeUs rounded to whole milliseconds, absolute (never @+).
 *   //! With \a previous only what changed since it; without, everything (a self-contained first frame). The commands
 *   //! are the ones above and the existing ones: hand left|right on|off, hand left|right fingers <5 curls> | on|off,
 *   //! action <name> left|right <x> [<y>] (a 2D action - thumbstick - always with both), session focused|visible|idle,
 *   //! profile <path>|none. Positions to 0.1 mm or better, angles to 0.01 degree, curls to 0.01 or better.
 *   std::string formatReplayFrame(const ReplayFrame & frame, const ReplayFrame * previous = nullptr);
 *
 *   //! What the replayed game is doing: for the anchors and the drift
 *   struct ReplayGame {
 *     bool playable = true; //!< in gameplay (no loading screen, no menu)
 *     SimPlayer player;     //!< the replayed player
 *   };
 *
 *   //! vr.sim.play.drift: how far the replayed player is from the recorded one
 *   struct ReplayDrift {
 *     float current = 0.f;  //!< meters, at the last frame compared
 *     float max = 0.f;      //!< meters, the largest so far
 *     u64 frames = 0;       //!< frames compared
 *     //! Compare one frame; only when both are in a level and in the same one (else nothing changes)
 *     void update(const SimPlayer & recorded, const SimPlayer & replayed);
 *   };
 *
 *   //! The status block vr.sim.play
 *   struct ClipStatus {
 *     std::string file;          //!< the clip's file name (the caller sets it; only the leaf, never a full path)
 *     double positionMs = 0.0;   //!< where the play is on the clip's clock
 *     u64 framesServed = 0;      //!< frame lines applied inside the play's window
 *     bool done = false;         //!< the window's end, the clip's end, play stop or an error
 *     u32 anchorsWaiting = 0;    //!< 1 while the play holds at an anchor
 *     ReplayDrift drift;
 *     u64 eventsReplayed = 0;    //!< discrete changes applied in the window: action values, session, profile,
 *                                //!< hand on/off, fingers on/off - poses and curls are not events
 *   };
 *
 *   //! The streaming clip player: a file cursor, never the whole clip in memory (a 30 minute clip is 50+ MB)
 *   class ClipPlayer {
 *   public:
 *     //! Gives the clip's next line (without the '\n'); false at the end of the clip
 *     using LineSource = std::function<bool(std::string & line)>;
 *     static constexpr u64 NoEnd = std::numeric_limits<u64>::max();
 *     //! Reads the header at once (up to the first frame line); \a fromUs / \a toUs: the window on the clip's clock
 *     explicit ClipPlayer(LineSource source, u64 fromUs = 0, u64 toUs = NoEnd);
 *     const ReplayHeader & header() const;
 *     //! The play at \a nowUs (the replay's display time since the play started, not decreasing): the clip's
 *     //! position is max(fromUs, the first frame's time) + nowUs minus the time held at anchors. Applies to \a sim,
 *     //! in order, the header's set-up lines (first call) and every frame line up to the position - the frames
 *     //! before fromUs at the first call, without counting them - reading no further than one line ahead.
 *     //! At an "anchor level <area>" the play holds (anchorsWaiting 1, the position stays at the anchor) until
 *     //! \a game is playable in that area; at "anchor save" until it is playable. The drift compares the clip's
 *     //! player with game.player after every call that applied a frame.
 *     void advance(u64 nowUs, const ReplayGame & game, Simulation & sim);
 *     const ClipStatus & status() const;  //!< file left empty
 *     u64 frameTimeUs() const;            //!< the clip time of the last frame line applied
 *     //! Empty, or "line <n>: <what>" (n counts every line of the file from 1) - a broken line ends the play
 *     const std::string & error() const;
 *   };
 *
 *   }
 *
 * The file and the session (checked in the game by scripts/dev/replay-check.ps1):
 *   - <user dir>\arx-replay.log while a VR session records; "gzip at session end": arx-replay.log.gz, the plain file
 *     removed; the next run rotates them as arx-perf.log is rotated: arx-replay.1.log.gz .. arx-replay.9.log.gz (a run
 *     that died keeps its plain arx-replay.log, rotated to arx-replay.<n>.log)
 *   - the record starts at the first frame the game is playable - the frame the simulator's gameplay timeline
 *     starts - with the save made then ("# save: ") and the header; frame times count from it
 *   - off with --no-replay-log (nothing written, `replay mark` refused) and in flat mode (--no-vr)
 *   - --seed <n>: Random::seed(n) on the main thread at start-up; the header's seed is the seed used either way
 *   - arx.log: "[replay] recording arx-replay.log: save <folder> \"<name>\", seed <n>, <hz> Hz" at the record start;
 *     "[replay] mark <name>: replay-marks\<file> (<n> frames, <s> s)" after a mark
 *   - a level change writes "@<t>ms anchor level <area>" when the new level is playable
 */
class ReplayClipTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(ReplayClipTest);
	CPPUNIT_TEST(testDialectCommands);
	CPPUNIT_TEST(testDialectRefusals);
	CPPUNIT_TEST(testSeveralCommandsOnOneLine);
	CPPUNIT_TEST(testHeaderRoundTrip);
	CPPUNIT_TEST(testFullFrameRoundTrip);
	CPPUNIT_TEST(testDeltaStreamRoundTrip);
	CPPUNIT_TEST(testValuesWithinTolerance);
	CPPUNIT_TEST(testClipLoadsAsAScript);
	CPPUNIT_TEST(testFrameSizeBudget);
	CPPUNIT_TEST(testPlayerReadsNoFurtherThanOneLineAhead);
	CPPUNIT_TEST(testThirtyMinuteClipAtMicrosecondsPerFrame);
	CPPUNIT_TEST(testWindowFromTo);
	CPPUNIT_TEST(testLevelAnchorHolds);
	CPPUNIT_TEST(testSaveAnchorHolds);
	CPPUNIT_TEST(testEventsReplayed);
	CPPUNIT_TEST(testDrift);
	CPPUNIT_TEST(testDriftThroughThePlayer);
	CPPUNIT_TEST(testBrokenLineEndsThePlay);
	CPPUNIT_TEST_SUITE_END();

public:

	void testDialectCommands();
	void testDialectRefusals();
	void testSeveralCommandsOnOneLine();
	void testHeaderRoundTrip();
	void testFullFrameRoundTrip();
	void testDeltaStreamRoundTrip();
	void testValuesWithinTolerance();
	void testClipLoadsAsAScript();
	void testFrameSizeBudget();
	void testPlayerReadsNoFurtherThanOneLineAhead();
	void testThirtyMinuteClipAtMicrosecondsPerFrame();
	void testWindowFromTo();
	void testLevelAnchorHolds();
	void testSaveAnchorHolds();
	void testEventsReplayed();
	void testDrift();
	void testDriftThroughThePlayer();
	void testBrokenLineEndsThePlay();

};

#endif // ARX_TESTS_VR_REPLAYCLIPTEST_H
