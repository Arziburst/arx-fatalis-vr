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
 *    used by the chord or the long press and was held shorter than \ref ControlResolver::longPressMs;
 *  - a chord fires when its second control is pressed while the first is held; both controls are
 *    then consumed until released (no tap, no long press, no single binding of the second);
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
	MagicMode,     //!< start drawing runes - the control is decided with the magic tasks (#43)
	Count
};

const size_t EventCount = size_t(Event::Count);

//! The text form of an event, e.g. "jump", "interact_left" - for cfg files, the log and status.json
const char * eventName(Event event);

//! The name players read, e.g. "Click / drag (left hand)"
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
 * The default layout for a controller type: the D-019 table (#35 finalizes the per-type defaults).
 * The Index and the Touch share the button positions; an unknown controller gets the Index layout,
 * because SteamVR's automatic rebinding emulates one of the profiles we suggest.
 */
ControlLayout defaultLayout(ControllerType type);

//! One "event = binding" line per event, in the order of \ref Event
std::string formatLayout(const ControlLayout & layout);

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

//! The events that share a binding - shown to the player when remapping (#178)
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
 * The Index grip as squeeze/force - the pressure after the fingers closed. Its squeeze/value comes
 * from finger contact and crossed 0.70 / 0.55 from just holding the controller (the owner's headset
 * run of #30: six "use" taps nobody made, TRAPS.md).
 */
const Hysteresis IndexGripForceThreshold = { 0.40f, 0.25f };

//! The grip threshold for the controllers of one hand, by the value their binding reports
const Hysteresis & gripThreshold(ControllerType type);
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
	std::array<Stick, HandCount> sticks;
	std::array<float, HandCount> trigger = { };
	std::array<float, HandCount> grip = { };
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

	ControlSet m_down;
	std::array<ControlState, ControlCount> m_controls;
	std::array<EventState, EventCount> m_events;

};

} // namespace vr

#endif // ARX_VR_VRCONTROLS_H
