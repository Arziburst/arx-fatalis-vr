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

#include "vr/VRInput.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>

#include "core/Config.h"
#include "core/Core.h"
#include "core/GameEvent.h"
#include "core/TimeTypes.h"
#include "game/Player.h"
#include "gui/Dragging.h"
#include "gui/Menu.h"
#include "input/Input.h"
#include "io/fs/FilePath.h"
#include "io/fs/Filesystem.h"
#include "io/fs/SystemPaths.h"
#include "io/log/Logger.h"
#include "platform/Time.h"
#include "util/Number.h"
#include "vr/BodyYaw.h"
#include "vr/HeadTracking.h"
#include "vr/MenuPointer.h"
#include "vr/VRMarks.h"
#include "vr/VRMelee.h"
#include "vr/VRRadial.h"
#include "vr/VRReticle.h"
#include "vr/VRSystem.h"
#include "vr/VRWeapon.h"

namespace vr {

namespace {

struct InputState {

	ControllerType type = ControllerType::Unknown;
	ControlResolver resolver = ControlResolver(defaultLayout(ControllerType::Unknown));
	ControllerInput input;
	GameInputMapper game;
	TurnController turn;
	bool smoothTurning = false; //!< a smooth turn is under way (for its start / stop game events)
	//! The push of the last snap (#285): when its first snap was made, its repeats, their mean interval
	PlatformInstant pushStart = 0;
	u64 pushRepeats = 0;
	float repeatInterval = 0.f;

	//! Controls set by the test channel, merged into what the controllers report
	ControlSet injectedDown;
	ControlSet injectedTaps;
	//! Stick deflections set by the test channel, added to the controllers' (directions: down / up)
	std::array<Stick, HandCount> injectedSticks;

	//! The saved layout of the first controller type was read (#178)
	bool loaded = false;

	//! The VR settings page's remapping (#178)
	BindingCapture capture;
	Event captureEvent = Event::Jump;
	u32 captures = 0;
	//! Assignments refused because they would have left the game menu without a binding (#326), and why the last was
	u32 remapRefusals = 0;
	std::string remapNote;

	//! The menu pointer (#34): the hand on the floating panel is the mouse
	MenuPointer pointer;
	bool panelShown = false;
	std::array<PointerRay, HandCount> rays;
	u64 loggedOffPanelPresses = 0;
	//! A hand's use event as the menus' right button (#398): held since a press with that hand on the panel
	std::array<bool, HandCount> useOnPanel = { };
	//! The pointing hand's A as the menus' right button (#496): held since a press with that hand on the panel
	std::array<bool, HandCount> aOnPanel = { };
	std::array<bool, HandCount> aDown = { };
	WalkStatus walk;

	//! The hand's use / interact chord acted on a door, lever, chest or body (#38): no use or click until released
	std::array<bool, HandCount> acting = { };
	//! The hand holds a drawn weapon in the world (#377): its trigger is no click (and charges nothing, #465)
	std::array<bool, HandCount> armed = { };

	//! The radial menu's game menu item holds Escape for this many more updates (#376)
	u32 radialEscapeUpdates = 0;
	//! A dragged item went into a radial quick slot: the click stays up until the triggers are let go (#376)
	bool radialClickReleased = false;

	//! The skip controls (#394) down in the last update, and those kept from the game until released
	ControlSet skipDown;
	ControlSet skipHeld;
	//! A scripted scene skips on a hold, not a press (#513): when the hold began, and whether it skipped already
	bool skipHolding = false;
	bool skipHoldFired = false;
	PlatformInstant skipHoldStart = 0;
	//! Skip controls held when the runtime bound them or when the scene started: no press until released (#513)
	ControlSet skipIgnored;
	bool skipWasPlaying = false;
	int lastSkipControl = -1; //!< the control of the last skip, for the vr.skip event; -1 none

};

InputState g_input;

//! How long A, B or a trigger is held to skip a scripted scene (#513): a press alone is no skip there
const float SceneSkipHoldSeconds = 0.6f;
//! A trigger skips a scripted scene only pulled this far (#513): the owner's lines were skipped by a light pull while
//! he held the controllers - the ordinary threshold is 0.55
const float SceneSkipTriggerPull = 0.9f;

//! \return the first control of \a set, -1 for none (the vr.skip event)
static int firstSkipControl(const ControlSet & set) {
	for(size_t c = 0; c < ControlCount; c++) {
		if(set.test(c)) {
			return int(c);
		}
	}
	return -1;
}

//! The controls that skip a logo or a cinematic (#394): A, B and the trigger of either hand
ControlSet skipControls() {
	ControlSet controls;
	for(size_t hand = 0; hand < HandCount; hand++) {
		for(ControlKind kind : { ControlKind::A, ControlKind::B, ControlKind::Trigger }) {
			controls.set(control(Hand(hand), kind));
		}
	}
	return controls;
}

//! What the skip controls would skip now (#394): the game state's name, "SCENE" for a scripted scene in the game, or
//! nullptr - always nullptr while a binding capture waits for its press
const char * skippable() {
	// A binding capture in the VR settings (#178) takes the next press, also while a scripted scene runs
	if(g_input.capture.active()) {
		return nullptr;
	}
	const gameevent::GameState state = gameevent::currentGameState();
	if(state == gameevent::GameState::Logo || state == gameevent::GameState::Intro
	   || state == gameevent::GameState::Cinematic) {
		return gameevent::gameStateName(state);
	}
	if(state == gameevent::GameState::Gameplay && scriptedScenePlaying()) {
		return "SCENE";
	}
	return nullptr;
}

/*!
 * While a logo, a cinematic or a scripted scene (the prison intro after a new game) plays (#394) A, B
 * and the triggers skip it and reach nothing else: they are taken out of the input before it is
 * resolved, and one still held when it ends stays out until it is released - the press that skipped
 * never clicks in the menu, opens a radial menu or jumps in the game that follows.
 *
 * \return true when one of them was pressed in this update while something skippable plays
 */
bool takeSkipControls(ControllerInput & input) {

	static const ControlSet controls = skipControls();
	const char * what = skippable();
	const bool playing = what != nullptr;
	const bool scene = playing && std::string_view(what) == "SCENE";

	// A control held when the runtime bound it (the controllers woken or re-bound mid-scene) or when the scene started
	// is no press, until it is released (#513)
	ControlSet ignoredNow = input.newlyBound & input.down;
	if(playing && !g_input.skipWasPlaying) {
		ignoredNow |= input.down;
	}
	g_input.skipWasPlaying = playing;
	g_input.skipIgnored = (g_input.skipIgnored | ignoredNow) & input.down & controls;
	const ControlSet pressed = ((input.down & ~g_input.skipDown) | input.taps) & controls & ~g_input.skipIgnored;
	const ControlSet heldNow = input.down & controls & ~g_input.skipIgnored;
	const std::array<float, HandCount> trigger = input.trigger;
	g_input.skipDown = input.down & controls;
	g_input.skipHeld = playing ? g_input.skipDown : (g_input.skipHeld & input.down);

	const ControlSet taken = playing ? controls : g_input.skipHeld;
	input.down &= ~taken;
	input.taps &= ~taken;
	for(size_t hand = 0; hand < HandCount; hand++) {
		if(taken.test(control(Hand(hand), ControlKind::Trigger))) {
			input.trigger[hand] = 0.f;
		}
	}

	g_input.lastSkipControl = -1;
	if(scene) {
		// A scripted scene - a dialogue - skips only on a deliberate hold (#513): A or B clicked, or a trigger pulled
		// all the way, for SceneSkipHoldSeconds. A logo, the intro and a cinematic still skip on a press (#394)
		ControlSet strong;
		for(size_t hand = 0; hand < HandCount; hand++) {
			for(ControlKind kind : { ControlKind::A, ControlKind::B }) {
				if(heldNow.test(control(Hand(hand), kind))) {
					strong.set(control(Hand(hand), kind));
				}
			}
			if(heldNow.test(control(Hand(hand), ControlKind::Trigger)) && trigger[hand] >= SceneSkipTriggerPull) {
				strong.set(control(Hand(hand), ControlKind::Trigger));
			}
		}
		if(!strong.any()) {
			g_input.skipHolding = false;
			return false;
		}
		const PlatformInstant now = platform::getTime();
		if(!g_input.skipHolding) {
			g_input.skipHolding = true;
			g_input.skipHoldFired = false;
			g_input.skipHoldStart = now;
		}
		if(!g_input.skipHoldFired && toS(now - g_input.skipHoldStart) >= SceneSkipHoldSeconds) {
			g_input.skipHoldFired = true;
			g_input.lastSkipControl = firstSkipControl(strong);
			return true;
		}
		return false;
	}
	g_input.skipHolding = false;

	if(playing && pressed.any()) {
		g_input.lastSkipControl = firstSkipControl(pressed);
		return true;
	}
	return false;
}
//! The right hand decides (the trigger hand of most players), the left one when the right has no profile
ControllerType typeFromProfiles(const std::array<std::string, HandCount> & profiles) {
	ControllerType right = controllerTypeFromProfile(profiles[size_t(Hand::Right)]);
	if(right != ControllerType::Unknown) {
		return right;
	}
	return controllerTypeFromProfile(profiles[size_t(Hand::Left)]);
}

void logConflicts(const ControlLayout & layout) {
	for(const Conflict & conflict : findConflicts(layout)) {
		LogWarning << "[vr] input: " << eventName(conflict.first) << " and " << eventName(conflict.second)
		           << " share " << formatBinding(layout[conflict.first]) << " - both fire";
	}
}

void applyLayout(const ControlLayout & layout, const char * why) {
	if(layout == g_input.resolver.layout()) {
		return;
	}
	g_input.resolver.setLayout(layout);
	LogInfo << "[vr] input: layout " << why;
	logConflicts(layout);
}

fs::path layoutPath(ControllerType type) {
	return fs::getConfigDir() / layoutFileName(type);
}

//! The layout the player saved for these controllers over their default, or the default (#178)
ControlLayout playerLayout(ControllerType type, std::string & why) {

	ControlLayout layout = defaultLayout(type);
	why = std::string("default for ") + controllerTypeName(type);

	const fs::path file = layoutPath(type);
	if(file.empty() || !fs::exists(file)) {
		return layout;
	}

	std::string error;
	if(!readSavedLayout(fs::read(file), type, layout, &error)) {
		// The default layout keeps the game playable; the file stays for the player to look at
		LogWarning << "[vr] input: ignoring the saved layout " << file << ": " << error;
		return layout;
	}

	why = std::string("saved for ") + controllerTypeName(type) + " (" + file.string() + ")";
	return layout;
}

void savePlayerLayout(const ControlLayout & layout) {

	const ControllerType type = layoutType(g_input.type);
	const fs::path file = layoutPath(type);
	if(file.empty()) {
		LogWarning << "[vr] input: no config directory - the remapped layout is not saved";
		return;
	}

	const std::string changes = formatLayoutChanges(layout, defaultLayout(g_input.type));
	if(changes.empty()) {
		if(fs::exists(file) && !fs::remove(file)) {
			LogWarning << "[vr] input: could not remove " << file;
		}
		return;
	}

	std::string text = std::string("# Arx Fatalis VR: the controls remapped in the VR settings for ")
	                   + controllerTypeName(type) + " controllers (#178).\n"
	                   "# The events that differ from the default layout; delete the file to reset them.\n";
	text += changes;
	if(!fs::write(file, text)) {
		LogWarning << "[vr] input: could not save the remapped layout to " << file;
	}
}

//! While the VR settings capture a binding the controls do nothing else (#178)
void updateCapture(s64 timeMs) {

	BindingCapture & capture = g_input.capture;
	if(!capture.active()) {
		return;
	}

	const Control refused = capture.refused();
	switch(capture.update(timeMs, g_input.input.down, g_input.input.taps)) {
		case BindingCapture::State::Done: {
			g_input.captures++;
			gameevent::Event("vr.remap")("event", eventName(g_input.captureEvent))
			                            ("binding", formatBinding(capture.binding()));
			setPlayerBinding(g_input.captureEvent, capture.binding());
			break;
		}
		case BindingCapture::State::Cancelled: {
			gameevent::Event("vr.remap")("event", eventName(g_input.captureEvent))("binding", "cancelled")
			                            ("why", "nothing pressed");
			break;
		}
		default: break;
	}

	if(capture.refused() != refused && capture.refused() != Binding::None) {
		LogInfo << "[vr] input: remapping " << eventName(g_input.captureEvent) << " refused "
		        << controlName(capture.refused()) << ": " << capture.refusedWhy();
	}
}

ControlAction engineAction(GameAction action) {
	switch(action) {
		case GameAction::Jump: return CONTROLS_CUST_JUMP;
		case GameAction::CrouchToggle: return CONTROLS_CUST_CROUCHTOGGLE;
		case GameAction::Inventory: return CONTROLS_CUST_INVENTORY;
		case GameAction::Book: return CONTROLS_CUST_BOOK;
		case GameAction::MagicMode: return CONTROLS_CUST_MAGICMODE;
		case GameAction::Stealth: return CONTROLS_CUST_STEALTHMODE;
		case GameAction::Use: return CONTROLS_CUST_USE;
		case GameAction::QuickSave: return CONTROLS_CUST_QUICKSAVE;
		case GameAction::QuickLoad: return CONTROLS_CUST_QUICKLOAD;
		case GameAction::CancelSpell: return CONTROLS_CUST_CANCELCURSPELL;
		case GameAction::BookMap: return CONTROLS_CUST_BOOKMAP;
		case GameAction::Count: break;
	}
	arx_unreachable();
}

//! Hand the snapshot to the engine's input, which applies it in its next update (this frame)
void applyToEngine(const GameInputState & state) {

	if(!GInput) {
		return;
	}

	// Both hands work as the mouse (D-016): the left button clicks and drags; on the floating panel the pointing
	// hand's pixel is the mouse, and a press off the panel clicks nothing (#34). A chord that acted on a door,
	// lever, chest or body (#38) is no click on it: the mapper left it out (HandRoles)
	GInput->setVirtualMouseButton(Mouse::Button_0, state.click && !g_input.pointer.suppressClick());
	if(g_input.pointer.active()) {
		const Vec2f cursor = g_input.pointer.cursor();
		// A world reticle (#37) is the exploration mode's crosshair too, and the panel's pixel the mouse on the book,
		// which stays in freelook in VR (#377): both also in the relative mouse mode
		const bool book = (player.Interface & INTER_PLAYERBOOK) != 0;
		GInput->setVirtualMousePosition(Vec2s(s16(std::lround(cursor.x)), s16(std::lround(cursor.y))),
		                                !g_input.pointer.onPanel() || book);
	} else {
		GInput->setVirtualMousePosition(std::nullopt);
	}
	// The original's right click: the use action in game, the right button in the menus (in game
	// the right button is free look) - there only from a hand on the panel, like the trigger (#398), and the
	// pointing hand's A, as no chord forms on a menu panel (#496)
	const bool useOnPanel = g_input.useOnPanel[size_t(Hand::Left)] || g_input.useOnPanel[size_t(Hand::Right)];
	const bool aOnPanel = g_input.aOnPanel[size_t(Hand::Left)] || g_input.aOnPanel[size_t(Hand::Right)];
	GInput->setVirtualMouseButton(Mouse::Button_1,
	                              ARXmenu.mode() != Mode_InGame && ((state.use && useOnPanel) || aOnPanel));
	GInput->setVirtualKey(Keyboard::Key_Escape, state.escape);
	for(size_t i = 0; i < GameActionCount; i++) {
		GInput->setVirtualAction(engineAction(GameAction(i)), state.actions[i]);
	}
}

/*!
 * The menu pointer (#34): where each hand's aim ray hits the floating panel this frame, which hand
 * owns the cursor (vr/MenuPointer.h) and the beam from it. Game events on the panel appearing and
 * going, on an owner change and on every press - bounded by the player's hands, never per frame.
 */
void updatePointer() {

	const bool shown = panelShown();
	const std::array<HandAim, HandCount> aims = handAims();

	// The hands' hidden rays in the world (#37): their reticles are targets of the same mouse
	updateReticles(aims, double(toUs(platform::getTime())) / 1000000.0);
	const bool world = reticlesOn();

	std::array<Vec3f, HandCount> hits = { };
	std::array<bool, HandCount> trigger = { };
	std::array<bool, HandCount> claims = { };
	for(size_t hand = 0; hand < HandCount; hand++) {
		PointerRay & ray = g_input.rays[hand];
		ray = PointerRay();
		if(shown && aims[hand].valid) {
			ray.onPanel = panelHit(aims[hand].origin, aims[hand].direction, ray.pixel, &hits[hand]);
		}
		if(!ray.onPanel) {
			reticleTarget(Hand(hand), ray);
		}
		claims[hand] = reticleClaims(Hand(hand));
	}
	// The act (#38, D-042): a hand's reticle on a door, lever, chest or body acts on it. Its chord is the use's
	// (#377), which then is neither a use nor a press of the pointer until it is released (the target decides)
	const Event actEvents[HandCount] = { Event::ActLeft, Event::ActRight };
	const Event useEvents[HandCount] = { Event::UseLeft, Event::UseRight };
	const Event stashEvents[HandCount] = { Event::StashLeft, Event::StashRight };
	const Event interactEvents[HandCount] = { Event::InteractLeft, Event::InteractRight };
	// The weapon hand (the right until #380) with a drawn weapon in the world: its trigger does nothing - no charge,
	// the swing is the strike (vr/VRMelee.h, active since the last frame, #465) - and clicks nothing (D-042); on the
	// panel it clicks as ever
	g_input.armed = { };
	g_input.armed[size_t(Hand::Right)] = meleeStatus().active && !g_input.rays[size_t(Hand::Right)].onPanel;
	for(size_t hand = 0; hand < HandCount; hand++) {
		if(g_input.resolver.pressed(actEvents[hand])) {
			g_input.acting[hand] = !g_input.rays[hand].onPanel && reticleAct(Hand(hand));
			// The hand that acts owns the cursor, as the use does
			claims[hand] = true;
		} else if(!g_input.resolver.active(stashEvents[hand]) && !g_input.resolver.active(actEvents[hand])
		          && !g_input.resolver.active(useEvents[hand])) {
			g_input.acting[hand] = false;
		}
		trigger[hand] = (g_input.resolver.active(interactEvents[hand]) && !g_input.armed[hand])
		                || (g_input.resolver.active(stashEvents[hand]) && !g_input.acting[hand]);
		// The use acts under its own hand (D-019): the hand that uses takes an idle cursor
		claims[hand] = claims[hand] || g_input.resolver.pressed(useEvents[hand]);
	}
	// The pointing hand's A is the menus' right button (#496, the owner): pressed with that hand on the panel, it
	// clicks until it is let go, and the hand takes the cursor so the click lands where it points
	const bool menu = ARXmenu.mode() != Mode_InGame;
	std::array<bool, HandCount> aPressed = { };
	for(size_t hand = 0; hand < HandCount; hand++) {
		const bool a = g_input.input.down[control(Hand(hand), ControlKind::A)];
		if(!a) {
			g_input.aOnPanel[hand] = false;
		} else if(!g_input.aDown[hand]) {
			aPressed[hand] = menu && g_input.rays[hand].onPanel && !g_input.capture.active();
			g_input.aOnPanel[hand] = aPressed[hand];
			claims[hand] = claims[hand] || aPressed[hand];
		}
		g_input.aDown[hand] = a;
	}

	MenuPointer & pointer = g_input.pointer;
	pointer.update(shown || world, g_input.rays, trigger, PointerParams(), claims);
	afterPointer(pointer, g_input.rays);

	// A hand's use event, whatever control it is bound to, is the right button in the menus (#398): it clicks
	// only while that hand pressed it on the panel; a press off the panel clicks nothing and is muted like the
	// trigger's
	for(size_t hand = 0; hand < HandCount; hand++) {
		if(!g_input.resolver.active(useEvents[hand])) {
			g_input.useOnPanel[hand] = false;
		} else if(g_input.resolver.pressed(useEvents[hand])) {
			g_input.useOnPanel[hand] = g_input.rays[hand].onPanel;
			if(menu && !g_input.rays[hand].onPanel) {
				pointer.mutePress();
			}
		}
	}

	const size_t owner = size_t(pointer.owner());
	if(pointer.onPanel()) {
		setPointerBeam(true, aims[owner].origin, hits[owner]);
	} else {
		setPointerBeam(false);
	}

	if(shown != g_input.panelShown) {
		g_input.panelShown = shown;
		gameevent::Event("vr.pointer")("state", shown ? "on" : "off")("hand", handName(pointer.owner()))
			("tracked", int(aims[0].valid) + int(aims[1].valid));
	}
	if(!shown && !world) {
		return;
	}
	if(pointer.ownerChanged()) {
		gameevent::Event("vr.pointer.owner")("hand", handName(pointer.owner()));
	}
	for(size_t hand = 0; hand < HandCount; hand++) {
		const bool pressed = trigger[hand] && pointer.pressedOnPanel(Hand(hand));
		if(pressed) {
			gameevent::Event("vr.pointer.click")("hand", handName(Hand(hand)))
				("x", g_input.rays[hand].pixel.x, 0)("y", g_input.rays[hand].pixel.y, 0);
		}
		if(aPressed[hand]) {
			gameevent::Event("vr.pointer.rightclick")("hand", handName(Hand(hand)))
				("x", g_input.rays[hand].pixel.x, 0)("y", g_input.rays[hand].pixel.y, 0);
		}
	}
	if(pointer.offPanelPresses() != g_input.loggedOffPanelPresses) {
		g_input.loggedOffPanelPresses = pointer.offPanelPresses();
		gameevent::Event("vr.pointer.muted")("hand", handName(pointer.owner()))
			("reason", world ? "no target" : "off the panel");
	}
}

} // anonymous namespace

void updateInput() {

	ControllerInput input;
	readControllers(input);

	// The layout follows the controllers: the one the player saved for them, else their default (#178)
	ControllerType type = typeFromProfiles(controllerProfiles());
	if((type != g_input.type && type != ControllerType::Unknown) || !g_input.loaded) {
		if(type == ControllerType::Unknown) {
			type = g_input.type;
		}
		g_input.loaded = true;
		g_input.type = type;
		std::string why;
		ControlLayout layout = playerLayout(type, why);
		LogInfo << "[vr] input: controllers " << controllerTypeName(type) << " - layout " << why;
		if(g_input.capture.active()) {
			// A capture for the old controllers' layout would land in the new one's file
			cancelCapture();
		}
		applyLayout(layout, why.c_str());
	}

	input.down |= g_input.injectedDown;
	input.taps |= g_input.injectedTaps & ~input.down;
	g_input.injectedTaps.reset();
	for(size_t hand = 0; hand < HandCount; hand++) {
		Stick & stick = input.sticks[hand];
		stick.x = std::clamp(stick.x + g_input.injectedSticks[hand].x, -1.f, 1.f);
		stick.y = std::clamp(stick.y + g_input.injectedSticks[hand].y, -1.f, 1.f);
	}

	const bool skip = takeSkipControls(input);

	// In the menus a hand pointing at the panel (the last update's rays) clicks with its trigger however firmly it
	// holds the grip: its chords are off (#496). The book, the bag and a container are in game: their use stays
	const bool menu = ARXmenu.mode() != Mode_InGame;
	for(size_t hand = 0; hand < HandCount; hand++) {
		g_input.resolver.setChords(Hand(hand), !(menu && g_input.rays[hand].onPanel));
	}
	g_input.resolver.update(toMsi(platform::getTime()), input.down, input.taps);
	g_input.input = input;

	// A press is a state change made by the player: one line each, bounded by how fast hands are (not while a capture takes the controls)
	for(size_t i = 0; i < EventCount; i++) {
		const Event event = Event(i);
		if(g_input.resolver.pressed(event) && !g_input.capture.active()) {
			LogInfo << "[vr] input: " << eventName(event) << " (" << formatBinding(g_input.resolver.layout()[event]) << ')';
		}
	}

	// Both trackpads pressed mark the moment, whatever the layout and the menus (#551)
	marks::update();

	// The hands on the floating panel (#34), before the events reach the engine: a press moves the
	// cursor under the pressing hand in the same update, so the click lands where it points
	updatePointer();

	// Events -> the engine's actions, mouse buttons and keys (#74); nothing while the VR settings
	// capture a binding, so pressing the menu's or the inventory's control only binds it (#178)
	// - also in the update that ends it, whose events were resolved with the old layout
	HandRoles roles;
	roles.armed = g_input.armed;
	roles.acts = g_input.acting;
	const GameInputState & resolved = g_input.game.update(g_input.resolver, g_input.input, roles);
	const bool capturing = g_input.capture.active();
	updateCapture(toMsi(platform::getTime()));
	const bool controlsTaken = capturing || g_input.capture.active();
	GameInputState game = controlsTaken ? GameInputState() : resolved;
	if(controlsTaken) {
		// The A that binds a control in the VR settings is no right click either (#496)
		g_input.aOnPanel = { };
	}

	// Draw / sheathe the weapon (#377, D-042: the stick click of the weapon hand), in the game only
	if(g_input.resolver.pressed(Event::Weapon) && !controlsTaken && ARXmenu.mode() == Mode_InGame) {
		toggleHeldWeapon();
	}

	// The radial menus on B (#376): their items press the engine's keys for one update, the game menu
	// holds Escape for two like the menu button above (the engine acts on its release)
	RadialRequests radial;
	// No casting stance while an item is dragged (the verifier): the hand holds it - its trigger would turn into the
	// pen, and the engine closes the bag when its magic mode ends, with the item still in the hand
	if(g_draggedEntity) {
		game.actions[size_t(GameAction::MagicMode)] = false;
	}
	// The casting stance's press (A) closes everything open first (#377, the owner): the radial menus, the book,
	// the bag, a container, a note - not the game menu - then the stance starts with the hands free
	if(g_input.resolver.pressed(Event::MagicMode) && !controlsTaken && ARXmenu.mode() == Mode_InGame
	   && !g_draggedEntity) {
		closeRadialMenus("stance");
		closeOpenPanels(radial, true);
	}
	updateRadialMenus(g_input.resolver, controlsTaken, radial);
	for(size_t i = 0; i < GameActionCount; i++) {
		game.actions[i] = game.actions[i] || radial.pulses[i];
	}
	if(radial.gameMenu) {
		g_input.radialEscapeUpdates = 2;
	}
	if(g_input.radialEscapeUpdates > 0) {
		game.escape = true;
		g_input.radialEscapeUpdates--;
	}
	if(radial.releaseClick) {
		g_input.radialClickReleased = true;
	}
	if(g_input.radialClickReleased) {
		// Up for the engine until the triggers are let go: the hand that dragged into a slot picks nothing up
		g_input.radialClickReleased = game.click;
		game.click = false;
	}
	applyToEngine(game);

	// The game skips what is on screen this frame (#394, core/ArxGame.cpp): the logo, the whole intro,
	// the cinematic (through cinematicEnd(), as Esc does), the scripted scene (through its skip handlers)
	if(skip) {
		g_vrSkipPressed = true;
		const char * what = skippable();
		gameevent::Event("vr.skip")("what", what ? what : "none")
			("control", g_input.lastSkipControl >= 0 ? controlName(Control(g_input.lastSkipControl)) : std::string("none"));
	}

	// Recenter (#28; unbound by default since D-042, the radial menu has it - the player may bind it): the head as
	// it is at the next sample becomes the character's eye and facing - horizontal and height, standing or seated
	// (D-018). Any
	// time, menus included: the reference is taken as soon as there is a head to take it from
	if(game.recenter) {
		resetHeadReference("button");
	}

	// The open radial menus into their images, while the frame's targets are bound (stereo frames only)
	drawRadialMenus();
}

PointerStatus pointerStatus() {
	const MenuPointer & pointer = g_input.pointer;
	PointerStatus status;
	status.shown = g_input.panelShown;
	status.active = pointer.active();
	status.owner = handName(pointer.owner());
	status.cursor = pointer.cursor();
	status.beam = pointerBeamShown();
	status.onPanel = { g_input.rays[0].onPanel, g_input.rays[1].onPanel };
	status.onWorld = { g_input.rays[0].onWorld, g_input.rays[1].onWorld };
	status.clicks = pointer.clicks();
	status.worldClicks = pointer.worldClicks();
	status.offPanelPresses = pointer.offPanelPresses();
	status.ownerChanges = pointer.ownerChanges();
	return status;
}

const GameInputState & gameInput() {
	return g_input.game.state();
}

const WalkStatus & walkStatus() {
	return g_input.walk;
}

void reportWalk(const StickWalk & walk, float yaw, float heading) {

	WalkStatus & status = g_input.walk;
	yaw -= 360.f * std::floor(yaw / 360.f);
	const bool walking = walk.moving();
	if(walking != status.walking) {
		if(walking) {
			status.walks++;
		}
		// An episode, not a frame: the stick left its dead zone, or came back to rest
		gameevent::Event("vr.walk", 200)("state", walking ? "start" : "end")("yaw", yaw, 1)
			("heading", heading, 1)("look", wrapSignedDegrees(yaw - heading), 1)
			("pace", walk.sneak ? "sneak" : "run");
	} else if(walking && walk.sneak != status.sneak) {
		// The deflection crossed a threshold of the pace (#33): one line per switch, not per frame
		status.paceChanges++;
		gameevent::Event("vr.walk", 200)("state", "pace")("pace", walk.sneak ? "sneak" : "run");
	}
	status.sneak = walking && walk.sneak;

	status.walking = walking;
	status.yaw = yaw;
	status.heading = heading;
	status.direction = walk.direction;
}

const ControlResolver & controls() {
	return g_input.resolver;
}

ControllerType controllerType() {
	return g_input.type;
}

const ControllerInput & controllerInput() {
	return g_input.input;
}

ControllerType layoutType(ControllerType type) {
	return type == ControllerType::Unknown ? ControllerType::Index : type;
}

std::string layoutFileName(ControllerType type) {
	return std::string("vr-controls-") + controllerTypeName(layoutType(type)) + ".ini";
}

bool layoutRemapped() {
	return g_input.resolver.layout() != defaultLayout(g_input.type);
}

void setPlayerBinding(Event event, const Binding & binding) {
	ControlLayout layout = g_input.resolver.layout();
	if(!assignBinding(layout, event, binding, &g_input.remapNote)) {
		g_input.remapRefusals++;
		LogInfo << "[vr] input: " << eventName(event) << " = " << formatBinding(binding) << " refused: "
		        << g_input.remapNote;
		return;
	}
	g_input.remapNote.clear();
	const std::string why = std::string(eventName(event)) + " = " + formatBinding(binding) + " (VR settings)";
	applyLayout(layout, why.c_str());
	savePlayerLayout(layout);
}

void restoreDefaultLayout() {
	g_input.remapNote.clear();
	cancelCapture();
	applyLayout(defaultLayout(g_input.type), "reset to the default (VR settings)");
	savePlayerLayout(g_input.resolver.layout());
	gameevent::Event("vr.remap")("event", "all")("binding", "default")
	                            ("controllers", controllerTypeName(layoutType(g_input.type)));
}

void startCapture(Event event) {
	g_input.remapNote.clear();
	cancelCapture();
	g_input.captureEvent = event;
	g_input.capture.longPressMs = g_input.resolver.longPressMs;
	g_input.capture.releaseDebounceMs = g_input.resolver.releaseDebounceMs;
	g_input.capture.start(toMsi(platform::getTime()), g_input.input.down);
	LogInfo << "[vr] input: remapping " << eventName(event) << " - waiting for a press";
}

void cancelCapture() {
	if(g_input.capture.active()) {
		g_input.capture.cancel();
		LogInfo << "[vr] input: remapping " << eventName(g_input.captureEvent) << " cancelled";
	}
}

const BindingCapture & capture() {
	return g_input.capture;
}

Event captureEvent() {
	return g_input.captureEvent;
}

u32 captureCount() {
	return g_input.captures;
}

u32 remapRefusals() {
	return g_input.remapRefusals;
}

const std::string & remapNote() {
	return g_input.remapNote;
}

TurnSettings turnSettings() {
	TurnSettings settings;
	settings.mode = config.input.vrSmoothTurn ? TurnMode::Smooth : TurnMode::Snap;
	settings.snapAngle = config.input.vrSnapTurnAngle;
	settings.smoothSpeed = config.input.vrSmoothTurnSpeed;
	settings.snapRepeat = config.input.vrSnapTurnRepeat;
	return settings;
}

float updateTurn(float seconds, bool allowed) {

	const TurnSettings settings = turnSettings();
	const float axis = g_input.game.state().turn;
	const u64 snaps = g_input.turn.snaps();
	// A capture of the VR settings takes the controls: the turn stick does not turn meanwhile (#178)
	const float yaw = g_input.turn.update(settings, axis, seconds, allowed && !g_input.capture.active());

	// A snap is a player action: one event each; a smooth turn is an episode: logged when it starts and stops
	if(g_input.turn.snaps() != snaps) {
		// The repeat measured by the clock: a test reads it instead of a count of snaps, which depends on when
		// the stick is let go (#285)
		const PlatformInstant now = platform::getTime();
		if(g_input.turn.lastSnapRepeated()) {
			g_input.pushRepeats++;
			g_input.repeatInterval = toS(now - g_input.pushStart) / float(g_input.pushRepeats);
		} else {
			g_input.pushStart = now;
			g_input.pushRepeats = 0;
			g_input.repeatInterval = 0.f;
		}
		gameevent::Event("vr.turn.snap", 1000)("dir", g_input.turn.lastSnap() > 0 ? "right" : "left")
			("deg", settings.snapAngle, 1)("repeat", g_input.turn.lastSnapRepeated() ? "yes" : "no")
			("count", g_input.turn.snaps());
	}
	const bool smoothTurning = settings.mode == TurnMode::Smooth && yaw != 0.f;
	if(smoothTurning != g_input.smoothTurning) {
		gameevent::Event("vr.turn.smooth", 1000)("state", smoothTurning ? "start" : "stop")
			("speed", settings.smoothSpeed, 1)("turnedDeg", g_input.turn.turned(), 1);
		g_input.smoothTurning = smoothTurning;
	}

	return yaw;
}

const TurnController & turnState() {
	return g_input.turn;
}

u64 snapPushRepeats() {
	return g_input.pushRepeats;
}

float snapRepeatInterval() {
	return g_input.repeatInterval;
}

namespace {

std::string_view nextWord(std::string_view & text) {
	size_t start = text.find_first_not_of(" \t");
	if(start == std::string_view::npos) {
		text = std::string_view();
		return text;
	}
	text.remove_prefix(start);
	size_t end = text.find_first_of(" \t");
	std::string_view word = text.substr(0, end);
	text = (end == std::string_view::npos) ? std::string_view() : text.substr(end);
	return word;
}

} // anonymous namespace

std::string inputCommand(std::string_view arguments) {

	std::string_view rest = arguments;
	std::string_view verb = nextWord(rest);

	if(verb == "down" || verb == "up" || verb == "tap") {
		std::string_view name = nextWord(rest);
		std::optional<Control> control = parseControl(name);
		if(!control) {
			return "vr-input " + std::string(verb) + ": unknown control \"" + std::string(name)
			       + "\" (e.g. right.a, left.grip, right.stick_down)";
		}
		if(verb == "down") {
			g_input.injectedDown[*control] = true;
		} else if(verb == "up") {
			g_input.injectedDown[*control] = false;
		} else {
			g_input.injectedTaps[*control] = true;
		}
		return std::string();
	}

	if(verb == "bind") {
		std::string_view name = nextWord(rest);
		std::optional<Event> event = parseEvent(name);
		if(!event) {
			return "vr-input bind: unknown event \"" + std::string(name) + "\"";
		}
		std::string error;
		std::optional<Binding> binding = parseBinding(rest, &error);
		if(!binding) {
			return "vr-input bind: " + error;
		}
		ControlLayout layout = g_input.resolver.layout();
		if(!assignBinding(layout, *event, *binding, &g_input.remapNote)) {
			g_input.remapRefusals++;
			LogInfo << "[vr] input: " << eventName(*event) << " = " << formatBinding(*binding) << " refused: "
			        << g_input.remapNote;
			return std::string();
		}
		g_input.remapNote.clear();
		std::string why = std::string(eventName(*event)) + " = " + formatBinding(*binding) + " (test channel)";
		applyLayout(layout, why.c_str());
		return std::string();
	}

	if(verb == "stick") {
		std::string_view hand = nextWord(rest);
		std::optional<float> x = util::toFloat(nextWord(rest));
		std::optional<float> y = util::toFloat(nextWord(rest));
		if((hand != "left" && hand != "right") || !x || !y) {
			return "vr-input stick: expected left|right <x> <y>, each -1 to 1 (e.g. stick left 0 1)";
		}
		Stick & stick = g_input.injectedSticks[size_t(hand == "left" ? Hand::Left : Hand::Right)];
		stick.x = std::clamp(*x, -1.f, 1.f);
		stick.y = std::clamp(*y, -1.f, 1.f);
		return std::string();
	}

	if(verb == "capture") {
		std::string_view name = nextWord(rest);
		std::optional<Event> event = parseEvent(name);
		if(!event) {
			return "vr-input capture: unknown event \"" + std::string(name) + "\"";
		}
		startCapture(*event);
		return std::string();
	}

	if(verb == "cancel") {
		cancelCapture();
		return std::string();
	}

	if(verb == "default") {
		restoreDefaultLayout();
		return std::string();
	}

	if(verb == "reload") {
		// Read the saved layout again at the next update, as when the controllers are detected at a start
		cancelCapture();
		g_input.loaded = false;
		return std::string();
	}

	if(verb == "turn") {
		// The turn settings as cfg.ini sets them (#32); the settings screen is #178
		std::string_view what = nextWord(rest);
		if(std::optional<TurnMode> mode = parseTurnMode(what)) {
			config.input.vrSmoothTurn = (*mode == TurnMode::Smooth);
		} else if(what == "angle" || what == "speed" || what == "repeat") {
			std::optional<float> value = util::toFloat(nextWord(rest));
			if(!value) {
				return "vr-input turn " + std::string(what) + ": expected a number";
			}
			if(what == "angle") {
				config.input.vrSnapTurnAngle = std::clamp(*value, MinSnapAngle, MaxSnapAngle);
			} else if(what == "repeat") {
				config.input.vrSnapTurnRepeat = std::clamp(*value, 0.f, MaxSnapRepeat);
			} else {
				config.input.vrSmoothTurnSpeed = std::clamp(*value, MinSmoothSpeed, MaxSmoothSpeed);
			}
		} else {
			return "vr-input turn: expected snap, smooth, angle <degrees>, speed <degrees per second> or repeat <seconds>, got \""
			       + std::string(what) + "\"";
		}
		const TurnSettings settings = turnSettings();
		LogInfo << "[vr] input: turn " << turnModeName(settings.mode) << ", snap " << settings.snapAngle
		        << " deg repeating every " << settings.snapRepeat << " s, smooth " << settings.smoothSpeed
		        << " deg/s (test channel)";
		return std::string();
	}

	if(verb == "height") {
		// The character height setting as cfg.ini sets it (#260): meters above the character's eye
		std::optional<float> value = util::toFloat(nextWord(rest));
		if(!value) {
			return "vr-input height: expected meters, e.g. 0.1";
		}
		config.input.vrHeightOffset = clampHeightOffset(*value);
		LogInfo << "[vr] input: character height " << config.input.vrHeightOffset << " m (test channel)";
		return std::string();
	}

	if(verb == "reset") {
		g_input.injectedDown.reset();
		g_input.injectedTaps.reset();
		g_input.injectedSticks = { };
		applyLayout(defaultLayout(g_input.type), "reset to the default (test channel)");
		return std::string();
	}

	return "vr-input: expected down|up|tap <control>, stick <hand> <x> <y>, bind <event> <binding>, turn <setting>,"
	       " height <meters>, reset, capture <event>, cancel, default or reload, got \"" + std::string(arguments) + "\"";
}

} // namespace vr
