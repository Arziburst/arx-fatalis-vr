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

#ifndef ARX_VR_VRCONTROLS_H
#define ARX_VR_VRCONTROLS_H

#include <array>
#include <bitset>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "platform/Platform.h"

/*!
 * \file
 * \brief The game's own control table: physical controls -> gameplay events (#30, D-019, D-020)
 *
 * OpenXR gives the game one action per physical control (trigger, grip, A, B, ... per hand, see
 * VRActions.h). This file decides which gameplay event each control produces, so players can
 * remap the controls inside the game (D-020): every event gets a single control, an ordered
 * chord of two controls on one hand (hold the first, press the second) or a long press.
 *
 * Pure logic - no OpenXR, no engine globals, no clock reads - covered by tests/vr/VRControlsTest.
 *
 * Press rules (F-122, F-43 in docs/vr/RESEARCH-reference-mods.md):
 *  - a single binding fires on the press, with no latency, unless its control also leads a chord
 *    or has a long press: then it fires on the release (a tap), and only if the control was not
 *    used by the chord or the long press; with a long press of its own it must also have been held
 *    shorter than \ref ControlResolver::longPressMs, a chord's leader taps after any hold (D-019:
 *    the grip is a closed hand, "use" when it opens without a trigger, stash with one);
 *  - a chord fires when its second control is pressed while the first is held; both controls are
 *    then consumed until released (no tap, no long press, no single binding of the second); with the
 *    hand's chords off (\ref ControlResolver::setChords) the second fires its single binding instead;
 *  - a long press fires once, when the control has been held for \ref ControlResolver::longPressMs;
 *  - the release of a control that leads a chord or has a long press counts only after it stayed up
 *    for \ref ControlResolver::releaseDebounceMs: a flicker is one hold, not a series of taps;
 *  - a press and release between two updates is delivered once (OpenXR's changedSinceLastSync);
 *  - an event that starts and ends in one update stays active for that update, so a game that
 *    polls once per frame sees it;
 *  - a control held while the layout changes is consumed until released.
 */
namespace vr {

enum class Hand : u8 {
	Left,
	Right
};

const size_t HandCount = 2;

//! "left" or "right"
const char * handName(Hand hand);

//! A physical control of one hand as a button: analog inputs become buttons past a threshold
enum class ControlKind : u8 {
	Trigger,
	Grip,
	A,          //!< the lower face button: A on the Index and the right Touch, X on the left Touch
	B,          //!< the upper face button: B on the Index and the right Touch, Y on the left Touch
	StickClick,
	StickUp,    //!< the thumbstick pushed in one direction
	StickDown,
	StickLeft,
	StickRight,
	Trackpad,   //!< a firm press on the Index trackpad
	Menu,       //!< the menu button (left Touch, simple controller)
	Count
};

const size_t ControlKindCount = size_t(ControlKind::Count);

//! A control of a hand, numbered hand * ControlKindCount + kind
typedef u8 Control;

const size_t ControlCount = HandCount * ControlKindCount;

//! A set of controls, indexed by \ref Control
typedef std::bitset<ControlCount> ControlSet;

inline Control control(Hand hand, ControlKind kind) {
	return Control(size_t(hand) * ControlKindCount + size_t(kind));
}

inline Hand controlHand(Control control) {
	return Hand(control / ControlKindCount);
}

inline ControlKind controlKind(Control control) {
	return ControlKind(control % ControlKindCount);
}

//! The text form of a control, e.g. "right.a", "left.stick_down"
std::string controlName(Control control);

//! \return the control named by \ref controlName, nothing for an unknown name
std::optional<Control> parseControl(std::string_view name);

//! A gameplay event the control table can trigger (the D-019 list); every concept its own event (F-139)
enum class Event : u8 {
	InteractLeft,  //!< click / drag with the left hand's reticle (the mouse's left button)
	InteractRight,
	UseLeft,       //!< use the item under the left reticle - eat, equip (the mouse's right button)
	UseRight,
	StashLeft,     //!< put the world item under the left reticle straight into the inventory
	StashRight,
	Jump,
	CrouchToggle,
	Inventory,
	Book,          //!< the character book: stats, map, diary, spells
	Menu,          //!< the game menu
	Recenter,
	MagicMode,     //!< held: the casting stance - a trigger draws runes with its hand (#43)
	ActLeft,       //!< act on the door, lever, chest, NPC or corpse under the left reticle (the double click, #38, D-042)
	ActRight,
	RadialLeft,    //!< held: the radial menu at the left hand, released: its item under the hand runs (#376, D-042; unbound by default, #377)
	RadialRight,
	Weapon,        //!< draw / sheathe the weapon of the weapon hand - the right one until #380 (#377, D-042)
	Map,           //!< the book on its map page, and closed again (#377, the owner: the left B)
	Count
};

const size_t EventCount = size_t(Event::Count);

//! The text form of an event, e.g. "jump", "interact_left" - for cfg files, the log and status.json
const char * eventName(Event event);

//! The name players read on the VR settings page (#178), short enough for its row, e.g. "Click / drag, left"
const char * eventLabel(Event event);

std::optional<Event> parseEvent(std::string_view name);

//! What triggers an event: one control, an ordered chord of two controls of one hand, a long press
struct Binding {

	static const Control None = 0xff;

	Control first = None;  //!< the control; of a chord, the one held first
	Control second = None; //!< of a chord, the control pressed while \ref first is held
	bool longPress = false; //!< fires once the control (or the chord) was held long enough

	bool isBound() const { return first != None; }
	bool isChord() const { return second != None; }

	bool operator==(const Binding & other) const {
		return first == other.first && second == other.second && longPress == other.longPress;
	}
	bool operator!=(const Binding & other) const { return !(*this == other); }

	static Binding single(Control control) {
		Binding binding;
		binding.first = control;
		return binding;
	}

	static Binding chord(Control first, Control second) {
		Binding binding;
		binding.first = first;
		binding.second = second;
		return binding;
	}

	static Binding hold(Control control) {
		Binding binding = single(control);
		binding.longPress = true;
		return binding;
	}

};

/*!
 * The text form of a binding: "none", "right.a", "left.grip+left.trigger" (a chord: hold the
 * first, press the second), "left.stick_click:long" (a long press, also of a chord)
 */
std::string formatBinding(const Binding & binding);

/*!
 * \return the binding \ref formatBinding wrote, or nothing with the reason in \a error: unknown
 *         control, a chord across hands or of one control twice, an unknown suffix
 */
std::optional<Binding> parseBinding(std::string_view text, std::string * error = nullptr);

//! The controllers a layout is made for - from the OpenXR interaction profile
enum class ControllerType : u8 {
	Unknown, //!< no profile yet, or one we do not suggest bindings for
	Index,   //!< /interaction_profiles/valve/index_controller
	Touch,   //!< /interaction_profiles/oculus/touch_controller (Quest 3 through SteamVR)
	Simple   //!< /interaction_profiles/khr/simple_controller
};

//! "unknown", "index", "touch" or "simple"
const char * controllerTypeName(ControllerType type);

/*!
 * Whether a controller type has the mark gesture of #551 (both trackpads held): the Index only - not the Touch, the
 * simple controller or one not identified yet. The game marks only when it is true for the controllers in use
 */
bool markGestureAvailable(ControllerType type);

//! \param profile an interaction profile path, e.g. "/interaction_profiles/valve/index_controller"
ControllerType controllerTypeFromProfile(std::string_view profile);

//! The control table: one binding per event
struct ControlLayout {

	std::array<Binding, EventCount> bindings;

	Binding & operator[](Event event) { return bindings[size_t(event)]; }
	const Binding & operator[](Event event) const { return bindings[size_t(event)]; }

	bool operator==(const ControlLayout & other) const { return bindings == other.bindings; }
	bool operator!=(const ControlLayout & other) const { return !(*this == other); }

};

/*!
 * The default layout for a controller type. The Index and the Touch: the D-042 table (#377), the same on
 * both hands and on both controllers (X / Y in A / B's places on the left Touch): the trigger clicks and
 * drags; grip, then trigger uses / acts (the reticle's target decides, \ref sharesBindingByDesign); the grip
 * alone does nothing; the right B holds the radial menu (#376), which has the book's pages, the game menu,
 * recenter and both hands' quick slots - the book and recenter have no control of their own; the left B
 * toggles the book's map page, the left A the bag (the owner, #456); the right stick up jumps, down toggles
 * the crouch; the right stick click draws / sheathes the weapon (the right hand's until #380); the trackpads
 * are free. The casting stance is the right A (the owner) until each hand has its own (#378). The Index and
 * the Touch layouts are the same: the Touch's left menu button is free too (the owner, #456).
 * The simple controller has select (click / drag) and the menu buttons: left = game menu,
 * long = recenter; right = inventory, long = book. An unknown controller gets the Index layout,
 * because SteamVR's automatic rebinding emulates one of the profiles we suggest.
 * Every control a layout uses is bound by its profile's suggestions (VRBindings.h, tested).
 */
ControlLayout defaultLayout(ControllerType type);

/*!
 * The name players read for a control on these controllers, e.g. "Right A", "Left Y" (the upper
 * button of the left Touch), "Left stick click"
 */
std::string controlLabel(Control control, ControllerType type);

/*!
 * The name players read for a binding, e.g. "Right B", "Left grip + Left trigger",
 * "Left stick click (hold)"; "---" when unbound (the game's own Customize Controls pages)
 */
std::string bindingLabel(const Binding & binding, ControllerType type);

//! One "event = binding" line per event, in the order of \ref Event
std::string formatLayout(const ControlLayout & layout);

/*!
 * One "event = binding" line per event whose binding differs from \a base - what a saved layout
 * keeps (#178), so a later change of a default reaches the events the player did not remap
 */
std::string formatLayoutChanges(const ControlLayout & layout, const ControlLayout & base);

/*!
 * A saved layout (#178): \a text (what \ref formatLayoutChanges wrote) read over the default of \a type.
 * A broken text - a line without "=", an unknown event, an unknown control - changes nothing: the
 * default layout comes back, with the reason (and the line) in \a error, so the game stays playable.
 * \return whether \a text was read
 */
bool readSavedLayout(std::string_view text, ControllerType type, ControlLayout & layout, std::string * error = nullptr);

/*!
 * Read "event = binding" lines (empty lines and lines starting with '#' are skipped) into \a layout;
 * events not named keep their binding.
 * \return false with the reason (and the line number) in \a error; \a layout is then unchanged
 */
bool parseLayout(std::string_view text, ControlLayout & layout, std::string * error = nullptr);

//! Two events with the same binding
struct Conflict {
	Event first;
	Event second;
};

/*!
 * Whether a binding of \a layout leads to the game menu (#326): the menu event, or a radial menu event (its ring has
 * the game menu, and where it cannot open its press is the game menu key, D-042) is bound
 */
bool menuReachable(const ControlLayout & layout);

/*!
 * The player's remapping (#178, #326): bind \a event to \a binding in \a layout - unless that leaves the game menu
 * without a binding (\ref menuReachable): then \a layout is unchanged and \a note is a short reason for the VR
 * settings page.
 * \return whether the binding was made
 */
bool assignBinding(ControlLayout & layout, Event event, const Binding & binding, std::string * note = nullptr);

/*!
 * Two events that may share a binding: one hand's act and its use or stash (#38, D-042, #377) - the reticle's
 * target decides which one acts (a door, lever, chest, NPC or corpse is acted on, an item is used or stashed)
 */
bool sharesBindingByDesign(Event a, Event b);

//! The events that share a binding - shown to the player when remapping (#178); not a pair of \ref sharesBindingByDesign
std::vector<Conflict> findConflicts(const ControlLayout & layout);

//! A press / release threshold pair for an analog input used as a button
struct Hysteresis {

	float press;
	float release;

	//! \return whether the button is down now, given whether it was down and the analog value
	bool update(bool down, float value) const {
		return down ? value > release : value >= press;
	}

};

const Hysteresis TriggerThreshold = { 0.55f, 0.40f };
//! The grip as squeeze/value (Touch, simple controller): a firm squeeze (F-41)
const Hysteresis GripThreshold = { 0.70f, 0.55f };
/*!
 * The Index grip as squeeze/value - how far the hand is closed around the handle. The controller is
 * strapped to the hand, so a light close is the grip and an open hand (the controller hanging on
 * the strap) is none (D-019, owner 2026-10-10). The relaxed closed hand flickered around 0.6 - 0.7
 * in the owner's first headset run of #30, against 0.70 / 0.55: six "use" events nobody made
 * (TRAPS.md); a wide gap keeps a closed hand closed. A firm hold of the controller rests at 0.52 - 0.59
 * (the owner's headset test of #460): against 0.50 every trigger press became the grip + trigger
 * chord, so the press is 0.70, above a resting hold, and the release 0.40, below the flicker of a
 * closed hand (#496). The player tunes both: vr_grip_press / vr_grip_release.
 */
const Hysteresis IndexGripThreshold = { 0.70f, 0.40f };

/*!
 * The grip threshold for the controllers of one hand, by the value their binding reports: \a index
 * for the Index (the player's vr_grip_press / vr_grip_release), \ref GripThreshold for the others
 */
Hysteresis gripThreshold(ControllerType type, const Hysteresis & index = IndexGripThreshold);
const Hysteresis TrackpadThreshold = { 0.40f, 0.25f };
const Hysteresis StickDirectionThreshold = { 0.70f, 0.50f };

/*!
 * The four stick directions of one hand as buttons: a direction is pressed when the stick is pushed
 * past the threshold along its axis and that axis dominates; it is released below the release
 * threshold. At most one direction is down at a time.
 */
void updateStickDirections(ControlSet & down, Hand hand, float x, float y);

//! A thumbstick: x right, y forward, each -1 to 1
struct Stick {
	float x = 0.f;
	float y = 0.f;
};

//! What the controllers did since the last read
struct ControllerInput {
	ControlSet down;  //!< the controls held now, analog ones past their threshold
	ControlSet taps;  //!< controls pressed and released since the last read
	ControlSet newlyBound; //!< controls the runtime bound in this read: one held then is no press (#513)
	std::array<Stick, HandCount> sticks;
	std::array<float, HandCount> trigger = { };
	std::array<float, HandCount> grip = { };
	std::array<bool, HandCount> thumbTouch = { }; //!< the thumb rests on a stick, trackpad or button (#305: closes the model's thumb)
};

//! Turns the controls held each update into gameplay events, by the rules at the top of this file
class ControlResolver {

public:

	//! How long a control is held for a long press, and the longest press that is still a tap
	s64 longPressMs = 500;

	/*!
	 * How long a control that leads a chord or has a long press must stay up before its release
	 * counts. A flicker of an analog control around its threshold (the Index grip from just holding
	 * the controller) is one hold that can still become the chord, not a series of taps; a press
	 * of the chord's second control inside the window still makes the chord. Controls with only a
	 * single binding are released at once.
	 */
	s64 releaseDebounceMs = 150;

	explicit ControlResolver(const ControlLayout & layout = ControlLayout());

	/*!
	 * Replace the layout. Every active event is released and every control held now is consumed
	 * until it is released (F-122: a button held across a mode switch does nothing).
	 */
	void setLayout(const ControlLayout & layout);
	const ControlLayout & layout() const { return m_layout; }

	/*!
	 * \param timeMs a monotonic time in milliseconds
	 * \param down   the controls held now
	 * \param taps   controls pressed and released since the last update (not in \a down)
	 */
	void update(s64 timeMs, const ControlSet & down, const ControlSet & taps = ControlSet());

	/*!
	 * Whether the chords of a hand form (on by default). Off, a control pressed while its chord's leader
	 * is held fires its own single binding instead, and the leader is consumed - no tap when it is let go
	 * (#496: on a menu panel the trigger clicks however firmly the hand holds the grip). A chord already
	 * on stays on until its controls are released.
	 */
	void setChords(Hand hand, bool on) { m_chords[size_t(hand)] = on; }
	bool chords(Hand hand) const { return m_chords[size_t(hand)]; }

	//! \return true while the event is on
	bool active(Event event) const { return m_events[size_t(event)].active; }
	//! \return true if the event came on in the last update
	bool pressed(Event event) const { return m_events[size_t(event)].pressed; }
	//! \return true if the event went off in the last update
	bool released(Event event) const { return m_events[size_t(event)].released; }
	//! \return how often the event came on since the resolver was created
	u32 presses(Event event) const { return m_events[size_t(event)].presses; }

	//! The controls held after the last update
	const ControlSet & down() const { return m_down; }

	/*!
	 * Both trackpads became down together in the last update: the mark of the moment (#551, the owner, 2026-10-20; the
	 * menu fallback of #326 on both stick clicks is gone). True for that one update, once per press - held on, nothing
	 * more; a release of either re-arms it. Fixed, not part of the layout and not rebindable: whatever the trackpads
	 * are bound to, both pressed mark (and their bindings act as well). Only where \ref markGestureAvailable.
	 */
	bool markGesture() const { return m_markGesture; }
	/*!
	 * Both trackpads are still down since the update the gesture came on: the push-to-talk of the mark's voice note.
	 * False from the first update either is not down, before any mark, with one alone and with both stick clicks
	 */
	bool markHeld() const { return m_markFired; }
	//! How often the gesture came on since the resolver was created
	u32 markGestures() const { return m_markGestures; }

private:

	struct ControlState {
		s64 since = 0;          //!< when the control was pressed
		s64 upSince = -1;       //!< a deferred control seen up since then, not released yet (-1: down)
		bool consumed = false;  //!< used by a chord or a long press - does nothing more until released
	};

	struct EventState {
		bool active = false;
		bool pressed = false;
		bool released = false;
		bool pulse = false;    //!< came on and its control went up in the same update: off next update
		s64 pending = -1;      //!< a long chord: when it was completed, -1 when not waiting
		u32 presses = 0;
	};

	void press(Control control, s64 timeMs);
	void release(Control control, s64 timeMs);
	void holdLong(s64 timeMs);

	void activate(Event event);
	void deactivate(Event event);

	ControlLayout m_layout;
	//! Controls whose single binding waits for the release: they lead a chord or have a long press
	ControlSet m_deferred;
	//! Controls with a long press of their own: their tap must be shorter than \ref longPressMs
	ControlSet m_hasLongPress;
	std::array<bool, HandCount> m_chords = { true, true };

	bool m_markFired = false;  //!< both trackpads are down and marked: no mark until one is let go
	bool m_markGesture = false;
	u32 m_markGestures = 0;

	ControlSet m_down;
	std::array<ControlState, ControlCount> m_controls;
	std::array<EventState, EventCount> m_events;

};

} // namespace vr

#endif // ARX_VR_VRCONTROLS_H
