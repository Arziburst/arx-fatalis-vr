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

#ifndef ARX_TESTS_VR_MARKMOMENTTEST_H
#define ARX_TESTS_VR_MARKMOMENTTEST_H

#include <cppunit/TestCase.h>
#include <cppunit/extensions/HelperMacros.h>

/*!
 * Mark the moment in the headset (#551; written before the implementation). During a headset test the owner presses
 * BOTH Index trackpads (a firm press or click: ControlKind::Trackpad): the mark fires at once, on the update both are
 * down - no hold (the owner, 2026-10-20; it was both stick clicks held 3 s, then both trackpads held 3 s). The game
 * writes a marker folder with everything a worker needs to reproduce the moment, and a confirmation shows in the
 * hero-dialogue panel (the subtitle panel of #460 / D-055). Both stick clicks are nothing special: the 2 s menu
 * fallback of D-058 (#539) stays removed (tests/vr/MenuReachTest.h) and they do not mark. A controller without
 * trackpads (the Touch) has no mark gesture. The feature is on in every build, the testers' too (the owner).
 *
 * The interface these tests expect (the voice note's half is in tests/vr/VoiceNoteTest.h):
 *
 * 1. The gesture, in src/vr/VRControls.h - where the menu fallback was (ControlResolver::menuFallbackMs,
 *    menuFallback() and menuFallbackOpens() go away with it, and so does markHoldMs - or it is 0):
 *
 *   //! Whether a controller type has the mark gesture: the Index (trackpads) only - not the Touch, the simple
 *   //! controller or one not identified yet. The game marks only when it is true for the controllers in use
 *   bool markGestureAvailable(ControllerType type);
 *
 *   class ControlResolver {
 *   public:
 *     //! Both trackpads became down together in the last update (#551, the owner): true for that one update, once
 *     //! per press - held on, nothing more; a release of either re-arms it, and the next update with both down
 *     //! marks again. One pressed, the other later: the mark on the update the second goes down. Fixed, not part
 *     //! of the layout and not rebindable: whatever the trackpads are bound to, both pressed mark (their bindings
 *     //! act as well). Both stick clicks do nothing of the kind
 *     bool markGesture() const;
 *     //! How often the gesture came on since the resolver was created
 *     u32 markGestures() const;
 *     //! Both trackpads are still down since the update the gesture came on (#551, push-to-talk): true from that
 *     //! update on, false from the first update either is not down - the release that stops the voice note
 *     //! (tests/vr/VoiceNoteTest.h: VoiceNote::update's \a held). False before any mark, with one trackpad alone and
 *     //! with both stick clicks; true again with the next mark (the released one pressed again)
 *     bool markHeld() const;
 *   };
 *
 * 2. The marker, in src/vr/MarkMoment.h (pure: no engine globals, no file system; the game's side is in
 *    src/vr/MarkMoment.cpp next to it, or wherever the worker wires it):
 *
 *   namespace vr {
 *
 *   //! The confirmation in the hero-dialogue panel: "Moment marked (#<number>)"
 *   std::string markNoticeText(u32 number);
 *   //! The microphone mark that leads the voice note's lines: U+1F3A4 (MICROPHONE) in UTF-8, "\xF0\x9F\x8E\xA4".
 *   //! The panel may draw it as an icon where the font has no glyph for it; the status text carries the character
 *
 *   //! The recording indicator while the note records, counting UP: the mark, a space, then the elapsed time as
 *   //! <minutes>:<seconds> - minutes without padding, seconds two digits, whole seconds rounded down; a negative time
 *   //! is 0:00. 0 -> "<mic> 0:00", 23999 -> "<mic> 0:23", 60000 -> "<mic> 1:00", 300000 -> "<mic> 5:00"
 *   std::string noteIndicatorText(s64 elapsedMs);
 *   //! The brief confirmation after a note was saved (about 1.5 s): "<mic> Note saved (<m>:<ss>)", the note's length
 *   //! in the same form - "<mic> Note saved (0:23)"
 *   std::string noteSavedText(s64 lengthMs);
 *
 *   //! The marker folder's name, <user dir>\marks\<name>\: the mark's UTC time (ISO 8601 basic) and its number in
 *   //! the session - "20261020T153012Z-1"; sorts by time, valid on every file system
 *   std::string markFolderName(s64 unixSeconds, u32 number);
 *
 *   //! What meta.txt says about a mark
 *   struct MarkMeta {
 *     u32 number = 0;               //!< the mark's number in this session, from 1 (the "#N" of the confirmation)
 *     s64 unixSeconds = 0;          //!< when it was made: UTC seconds since 1970
 *     u32 area = 0;                 //!< the level (status.json "area", g_currentArea), 0 for none
 *     Vec3f position = Vec3f(0.f);  //!< the player's position (status.json player.x / y / z), game units
 *     float yaw = 0.f;              //!< the player's heading (status.json player.yaw), degrees
 *     std::string build;            //!< the build: version, commit and config, as the game names itself
 *     std::string state;            //!< the game's state (status.json "state": GAMEPLAY, MENU, ...)
 *     std::string session;          //!< the VR session's state (focused, visible, synchronized, ...)
 *   };
 *
 *   //! meta.txt: a comment line, then one key=value line per field, in this order, each ending in "\n":
 *   //!   # moment mark (#551)
 *   //!   mark=<number>
 *   //!   time=<YYYY-MM-DDTHH:MM:SSZ>              UTC
 *   //!   area=<area>
 *   //!   position=<x> <y> <z>                     one decimal each ("%.1f")
 *   //!   yaw=<yaw>                                one decimal
 *   //!   build=<build>
 *   //!   state=<state>
 *   //!   session=<session>
 *   //!   kind=<press|hold>  pressMs=<ms>  releaseMs=<ms>   one line each (#574, tests/vr/MarkHoldTest.h)
 *   //! A control character (a new line, a tab) in a text field is written as a space, so a field is always one line.
 *   //! No path of any kind is in it (the marks go to a coordinator, the repository is public).
 *   std::string formatMarkMeta(const MarkMeta & meta);
 *
 *   }
 *
 * 3. The game (asserted by scripts/dev/xrs/mark-moment.xrs, the folder listed by scripts/dev/marks.ps1 and its
 *    self-test scripts/dev/test-marks.ps1). #574 changed the folder: a short press has no clip.vrsim and no note; a
 *    hold adds the snapshots and its own clip, and the note is at most 1 minute - tests/vr/MarkHoldTest.h has it:
 *    - only in VR, when ControlResolver::markGesture() comes on: <user dir>\marks\<markFolderName>\ with
 *        left.png, right.png   the eye images of that frame, or the nearest rendered one
 *        recorder.txt          the flight recorder's tail (#545: serializeRecorder of the live recorder)
 *        clip.vrsim            the replay ring's last 30 s, via the same code as the channel's `replay mark` (#550)
 *        meta.txt              formatMarkMeta
 *        note.wav              the voice note (tests/vr/VoiceNoteTest.h), when it is on and a microphone recorded at
 *                              least VoiceNote::minMs of it while both trackpads were held
 *    - the hero-dialogue panel shows markNoticeText(N) for about 2 s; while the note records (both trackpads still
 *      held, push-to-talk) noteIndicatorText(the elapsed time) instead, counting up; when a note.wav was saved (a
 *      release, or the 5 minutes) noteSavedText(its length) for about 1.5 s, then nothing. A press too short for a
 *      note (or no microphone) shows no indicator after the release and no "Note saved": the mark's confirmation only
 *    - the log: "[vr] mark #<N>: marks\<folder> files=<lastFiles>" once the five files are written, and the panel's
 *      "vr.scene.subtitle notice=<marked|recording|saved> ... text="<the line>"" when a notice shows
 *    - status.json (src/core/CommandChannel.cpp):
 *        marks.count      (count)  marks written since the start
 *        marks.last       (text)   the last mark's folder name (markFolderName), "" before the first
 *        marks.lastFiles  (text)   the non-empty files in that folder, sorted by name, joined by "," without spaces:
 *                                  "clip.vrsim,left.png,meta.txt,recorder.txt,right.png" (",note.wav" sorts in)
 *        marks.recording  (bool)   a voice note records now - only ever while both trackpads are held
 *        marks.noteMs     (number) how long the note records so far, in ms (what the indicator shows); 0 when none
 *                                  records
 *        marks.note       (bool)   the voice note is on (cfg.ini vr_mark_note, the channel's `marks note on|off`)
 *        marks.gesture    (text)   the gesture of the controllers in use: both.trackpad (the Index), none where
 *                                  markGestureAvailable is false (the Touch)
 *        scene.subtitle.notice (text) the notice the hero-dialogue panel shows now: none | marked | recording | saved;
 *                                  while it is not none, scene.subtitle.visible is true, scene.subtitle.text is the
 *                                  line (markNoticeText / noteIndicatorText / noteSavedText) and scene.subtitle.drawn
 *                                  is chest
 *    - cfg.ini vr_mark_note (bool, default 1): the voice note after a mark; 0 turns it off
 *    - channel: `marks note on|off` - the voice note for this session (cfg.ini keeps the player's choice)
 */
class MarkMomentTest final : public CppUnit::TestFixture {

	CPPUNIT_TEST_SUITE(MarkMomentTest);
	CPPUNIT_TEST(gestureAtOnceTest);
	CPPUNIT_TEST(gestureOncePerPressTest);
	CPPUNIT_TEST(gestureSecondLaterTest);
	CPPUNIT_TEST(gestureOneTrackpadTest);
	CPPUNIT_TEST(gestureSticksDoNotMarkTest);
	CPPUNIT_TEST(gestureTouchTest);
	CPPUNIT_TEST(gestureAnyLayoutTest);
	CPPUNIT_TEST(markHeldTest);
	CPPUNIT_TEST(noticeTextTest);
	CPPUNIT_TEST(folderNameTest);
	CPPUNIT_TEST(metaFormatTest);
	CPPUNIT_TEST(metaOneLinePerFieldTest);
	CPPUNIT_TEST(replayAndSimulatorTest);
	CPPUNIT_TEST(noticeTransitionTest);
	CPPUNIT_TEST(gestureNotTheTrackpadsBindingsTest);
	CPPUNIT_TEST_SUITE_END();

public:

	void gestureAtOnceTest();
	void gestureOncePerPressTest();
	void gestureSecondLaterTest();
	void gestureOneTrackpadTest();
	void gestureSticksDoNotMarkTest();
	void gestureTouchTest();
	void gestureAnyLayoutTest();
	void markHeldTest();
	void noticeTextTest();
	void folderNameTest();
	void metaFormatTest();
	void metaOneLinePerFieldTest();
	//! The coordinator's rules (#551): no mark while a replay clip plays, no microphone under the simulated headset
	void replayAndSimulatorTest();
	//! The panel from the release to "Note saved": the indicator frozen while the note.wav is written, no blank (#551)
	void noticeTransitionTest();
	/*!
	 * The toggle (#632, the coordinator's decision): the press of both trackpads that starts or stops a recording is
	 * not also a trackpad's own binding - pressed in one update nothing of theirs acts; one pressed first acts only
	 * as far as it already did alone (a single press at once), its deferred tap and its long press do not follow. A
	 * trackpad alone acts as bound
	 */
	void gestureNotTheTrackpadsBindingsTest();

};

#endif // ARX_TESTS_VR_MARKMOMENTTEST_H
