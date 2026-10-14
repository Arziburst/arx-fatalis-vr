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

#include "vr/VRRadial.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "cinematic/CinematicController.h"
#include "core/Config.h"
#include "core/GameEvent.h"
#include "core/Localisation.h"
#include "game/Camera.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/Inventory.h"
#include "game/Item.h"
#include "game/Player.h"
#include "graphics/Color.h"
#include "graphics/Draw.h"
#include "graphics/Renderer.h"
#include "graphics/Vertex.h"
#include "graphics/data/TextureContainer.h"
#include "graphics/font/Font.h"
#include "gui/Dragging.h"
#include "gui/Interface.h"
#include "gui/Menu.h"
#include "gui/Note.h"
#include "gui/Text.h"
#include "gui/book/Book.h"
#include "gui/hud/SecondaryInventory.h"
#include "io/log/Logger.h"
#include "script/Script.h"
#include "util/Number.h"
#include "vr/HeadPose.h"
#include "vr/HeadTracking.h"
#include "vr/VRSimulator.h"
#include "vr/VRSystem.h"

namespace vr {

namespace {

//! A torch lit by a menu's slot belongs to that hand once the engine lights it, within this many frames
const u32 TorchWaitFrames = 90;

const char * const SlotVariables[HandCount] = { "$vr_quickslots_left", "$vr_quickslots_right" };

struct HandMenu {
	bool open = false;
	RadialPose pose;
	RadialSelector selector;
	Vec2f point = Vec2f(0.f);
	Vec2f origin = Vec2f(0.f); //!< the hand in the ring's plane when it opened: the middle (#377)
	float travel = 0.f;      //!< the farthest the hand went from the middle while open, metres (the log, #377)
	u32 openFrames = 0;      //!< updates the menu was open (the log: drawn / submitted against them, #377)
	u32 drawnFrames = 0;     //!< of them, frames its image was drawn
	u32 submittedFrames = 0; //!< of them, frames its quad layer was in the submitted frame
	Vec3f hand = Vec3f(0.f); //!< the grip this frame, LOCAL
};

struct RadialState {
	RadialParams params;
	std::array<HandMenu, HandCount> menus;
	RadialStatus status;
	//! A torch slot was used: the torch lit by it is that hand's (the engine lights it from the item's script)
	struct TorchCandidate {
		bool waiting = false;
		Hand hand = Hand::Right;
		std::string className;
		u32 frames = 0;
	} torch;
	//! The names of the slots' kinds as last seen in the bag, for a slot with none left
	std::map<std::string, std::string> names;
};

RadialState g_radial;

Event radialEvent(Hand hand) {
	return hand == Hand::Left ? Event::RadialLeft : Event::RadialRight;
}

//! Why a menu cannot be open now, or nullptr
const char * closedReason(Hand hand) {
	// A menu first: with or without a hero, a press there is the game menu key (isGameMenuKey)
	if(ARXmenu.mode() != Mode_InGame) {
		return "menu";
	}
	if(!entities.player()) {
		return "no_player";
	}
	if(isInCinematic()) {
		return "cinematic";
	}
	if(!frameViews().valid) {
		return "no_head";
	}
	if(!frameHands().tracked[size_t(hand)]) {
		return "hand_not_tracked";
	}
	return nullptr;
}

/*!
 * A press refused for this reason is the game menu key, Escape (#377): a menu is shown - the game menu, one of its
 * pages, the character creation, the credits: Escape goes back, resumes or leaves them (the Index has no other key
 * for it since D-042) - or in the game the head or the hand is not tracked: Escape opens the game menu. Not in a
 * cinematic (the skip controls have it, #394) and not in the game without a hero (a load)
 */
bool isGameMenuKey(const char * reason) {
	const std::string_view why = reason;
	return why == "menu" || why == "no_head" || why == "hand_not_tracked";
}

Vec3f eyeMiddle() {
	const FrameViews & views = frameViews();
	return (views.eyes[0].position + views.eyes[1].position) * 0.5f;
}

/*!
 * Where a hand's ring opens (#377, cfg.ini vr_radial_place - a variant for the owner to pick in the headset): at the
 * hand ("hand", the default), or ("gaze_when_off") before the eyes at the hand's height when the hand is more than
 * vr_radial_gaze_deg off the gaze - a hand held beside the head (pointing at the panel) opened its ring out of view.
 * \param beforeEyes set when the ring was moved before the eyes
 */
Vec3f radialAnchor(const Vec3f & hand, const Vec3f & eye, bool & beforeEyes) {
	beforeEyes = false;
	const Vec3f toHand = hand - eye;
	if(config.input.vrRadialPlace != "gaze_when_off" || glm::length(toHand) < 1e-3f) {
		return hand;
	}
	const Vec3f forward = frameViews().eyes[0].orientation * Vec3f(0.f, 0.f, -1.f);
	const float cosine = std::clamp(glm::dot(forward, glm::normalize(toHand)), -1.f, 1.f);
	if(glm::degrees(std::acos(cosine)) <= config.input.vrRadialGazeDeg) {
		return hand;
	}
	// Ahead along the gaze on the floor's plane, as far out as the hand is (at least an arm's comfortable reach)
	Vec3f ahead(forward.x, 0.f, forward.z);
	ahead = glm::length(ahead) > 1e-3f ? glm::normalize(ahead) : Vec3f(0.f, 0.f, -1.f);
	const float reach = std::max(glm::length(Vec2f(toHand.x, toHand.z)), 0.35f);
	beforeEyes = true;
	return Vec3f(eye.x, hand.y, eye.z) + ahead * reach;
}

struct SlotItem {
	Entity * item = nullptr; //!< one of the kind in the bag
	long count = 0;          //!< how many of the kind the bag holds
};

SlotItem findInInventory(const std::string & className) {
	SlotItem found;
	Entity * owner = entities.player();
	if(className.empty() || !owner || !owner->inventory) {
		return found;
	}
	for(auto slot : owner->inventory->slots()) {
		if(!slot.entity || !slot.show || slot.entity->className() != className) {
			continue;
		}
		if(!found.item) {
			found.item = slot.entity;
		}
		found.count += (slot.entity->_itemdata && slot.entity->_itemdata->count > 0) ? slot.entity->_itemdata->count : 1;
	}
	return found;
}

void saveQuickSlots(Hand hand, const QuickSlotSet & slots) {
	SETVarValueText(svar, SlotVariables[size_t(hand)], formatQuickSlots(slots));
}

std::string itemName(const Entity * item) {
	if(!item) {
		return std::string();
	}
	if(!item->locname.empty()) {
		const std::string_view name = getLocalised(item->locname);
		if(!name.empty()) {
			return std::string(name);
		}
	}
	return item->className();
}

void putTorchOut(Entity * torch) {
	// What the torch key does (gui/Interface.cpp): the lit torch goes back into the bag
	InventoryPos pos = locateInInventories(torch);
	torch->setOwner(nullptr);
	giveToPlayer(torch, pos);
}

//! A quick slot released on: fill it with a dragged item, or use what it holds - for this hand
std::string runQuickSlot(Hand hand, u8 slot, RadialRequests & requests) {

	QuickSlotSet slots = quickSlots(hand);
	const std::string slotName = "slot" + std::to_string(int(slot) + 1);

	if(Entity * dragged = g_draggedEntity; dragged && (dragged->ioflags & IO_ITEM)) {
		slots[slot] = dragged->className();
		saveQuickSlots(hand, slots);
		// The item itself goes back where it came from: the slot names its kind, not this one. The trigger
		// that dragged it is still held: the engine must not see it as a press on whatever is under the hand
		const InventoryPos from = g_draggedItemPreviousPosition;
		setDraggedEntity(nullptr);
		const bool inBag = giveToPlayer(dragged, from);
		requests.releaseClick = true;
		gameevent::Event("vr.radial.slot")("hand", handName(hand))("slot", slotName)("item", slots[slot])
			("back", inBag ? "inventory" : "dropped");
		return "slot_filled";
	}

	const std::string & className = slots[slot];
	if(className.empty()) {
		return "empty_slot";
	}

	if(Entity * torch = player.torch; torch && torch->className() == className) {
		putTorchOut(torch);
		g_radial.status.torchHand = "none";
		gameevent::Event("vr.radial.use")("hand", handName(hand))("slot", slotName)("item", className)("result", "torch_out");
		return "torch_out";
	}

	const SlotItem found = findInInventory(className);
	if(!found.item) {
		gameevent::Event("vr.radial.use")("hand", handName(hand))("slot", slotName)("item", className)
			("result", "nothing_left");
		return "nothing_left";
	}

	useInventoryItemWithLowestDurability(className);
	g_radial.torch.waiting = true;
	g_radial.torch.hand = hand;
	g_radial.torch.className = className;
	g_radial.torch.frames = 0;
	gameevent::Event("vr.radial.use")("hand", handName(hand))("slot", slotName)("item", className)("result", "used")
		("left", found.count - 1);
	return "used";
}

/*!
 * A page of the book from the menu, as its key does: the book opens on it, or turns to it - and the same
 * item closes the book it shows (the owner's headset run: what an item opened, it closes)
 * \return "book_open", "book_closed", or "no_spells" for the spells page of a hero who knows no rune
 */
std::string toggleBookPage(ARX_INTERFACE_BOOK_MODE page) {
	const bool wasOpen = (player.Interface & INTER_PLAYERBOOK) != 0;
	g_playerBook.openPage(page, true);
	const bool open = (player.Interface & INTER_PLAYERBOOK) != 0;
	if(open) {
		return "book_open";
	}
	return wasOpen ? "book_closed" : (page == BOOKMODE_SPELLS ? "no_spells" : "book_closed");
}

//! Run the item a hand released B over; \return what it did, for the status
std::string runChoice(Hand hand, const RadialChoice & choice, RadialRequests & requests) {
	switch(choice.item) {
		case RadialItem::None: {
			// Over nothing, with the book, the bag, a container or a note open: B closes it (#377, the owner's
			// D-042 amendment) - not while an item is dragged, which would fall out of the bag
			if(!g_draggedEntity && !closeOpenPanels(requests).empty()) {
				return "closed";
			}
			return "none";
		}
		case RadialItem::Inventory: {
			requests.pulses[size_t(GameAction::Inventory)] = true;
			return "inventory";
		}
		case RadialItem::QuickSlot: {
			return runQuickSlot(hand, choice.slot, requests);
		}
		case RadialItem::GameMenu: {
			// Escape closes an open book or note first: the menu item opens the menu itself
			g_playerBook.close();
			if(g_note.isOpen()) {
				ARX_INTERFACE_NoteClose();
			}
			requests.gameMenu = true;
			return "game_menu";
		}
		case RadialItem::Character: {
			return toggleBookPage(BOOKMODE_STATS);
		}
		case RadialItem::Spells: {
			return toggleBookPage(BOOKMODE_SPELLS);
		}
		case RadialItem::Map: {
			return toggleBookPage(BOOKMODE_MINIMAP);
		}
		case RadialItem::Quests: {
			return toggleBookPage(BOOKMODE_QUESTS);
		}
		case RadialItem::QuickSave: {
			requests.pulses[size_t(GameAction::QuickSave)] = true;
			return "quick_save";
		}
		case RadialItem::QuickLoad: {
			requests.pulses[size_t(GameAction::QuickLoad)] = true;
			return "quick_load";
		}
		case RadialItem::Recenter: {
			resetHeadReference("radial");
			return "recenter";
		}
		case RadialItem::CancelSpell: {
			requests.pulses[size_t(GameAction::CancelSpell)] = true;
			return "cancel_spell";
		}
	}
	return "none";
}

void closeMenu(Hand hand, const char * why) {
	HandMenu & menu = g_radial.menus[size_t(hand)];
	RadialHandStatus & status = g_radial.status.hands[size_t(hand)];
	menu.open = false;
	status.open = false;
	status.halfRing = false;
	status.hovered = "none";
	gameevent::Event("vr.radial")("hand", handName(hand))("state", "close")("why", why);
}

//! The torch a slot's use lit: once the engine has lit it, it is that hand's
void followTorch() {
	RadialState::TorchCandidate & candidate = g_radial.torch;
	if(!player.torch) {
		g_radial.status.torchHand = "none";
	}
	if(!candidate.waiting) {
		return;
	}
	if(player.torch && player.torch->className() == candidate.className) {
		candidate.waiting = false;
		g_radial.status.torchHand = handName(candidate.hand);
		gameevent::Event("vr.radial.torch")("hand", handName(candidate.hand))("item", candidate.className);
	} else if(++candidate.frames > TorchWaitFrames) {
		candidate.waiting = false;
	}
}

} // anonymous namespace

QuickSlotSet quickSlots(Hand hand) {
	return parseQuickSlots(GETVarValueText(svar, SlotVariables[size_t(hand)]));
}

std::string closeOpenPanels(RadialRequests & requests, bool direct) {

	std::string closed;
	const auto note = [&closed](const char * what) {
		closed += closed.empty() ? what : std::string(" ") + what;
	};

	if(ARXmenu.mode() != Mode_InGame || !entities.player()) {
		return closed;
	}
	if(player.Interface & INTER_PLAYERBOOK) {
		g_playerBook.close();
		note("book");
	}
	if(g_note.isOpen()) {
		ARX_INTERFACE_NoteClose();
		note("note");
	}
	if(g_secondaryInventoryHud.isOpen()) {
		g_secondaryInventoryHud.close();
		note("container");
	}
	if(player.Interface & (INTER_INVENTORY | INTER_INVENTORYALL)) {
		if(direct) {
			// The casting stance starts in this update, and the engine leaves the bag alone in its magic mode: closed
			// here, with the engine's own state of it (the inverted bag, the cursor mode) set back to freelook
			bInverseInventory = false;
			InventoryOpenClose(2);
			TRUE_PLAYER_MOUSELOOK_ON = true;
			// The engine finishes a bag's closing only outside the magic mode (PlayerInventoryHud::update): the bag
			// is gone for the game now, and its slide-out ends after the stance
			player.Interface &= ~(INTER_INVENTORY | INTER_INVENTORYALL);
		} else {
			// The bag through its key, as the menu's inventory item: the engine keeps its own state of the bag
			requests.pulses[size_t(GameAction::Inventory)] = true;
		}
		note("inventory");
	}
	if(!closed.empty()) {
		gameevent::Event("vr.panels.closed")("what", closed);
	}
	return closed;
}

void closeRadialMenus(const char * why) {
	for(size_t i = 0; i < HandCount; i++) {
		if(g_radial.menus[i].open) {
			closeMenu(Hand(i), why);
		}
	}
}

void updateRadialMenus(const ControlResolver & controls, bool blocked, RadialRequests & requests) {

	followTorch();

	for(size_t i = 0; i < HandCount; i++) {

		const Hand hand = Hand(i);
		HandMenu & menu = g_radial.menus[i];
		RadialHandStatus & status = g_radial.status.hands[i];
		const Event event = radialEvent(hand);
		const char * reason = closedReason(hand);

		if(!menu.open) {
			if(!controls.pressed(event) || blocked) {
				continue;
			}
			if(reason) {
				// A press refused, once per press: the reason with it
				status.refused = reason;
				gameevent::Event("vr.radial.refused", 200)("hand", handName(hand))("reason", reason);
				if(isGameMenuKey(reason)) {
					// The menu is the only way to the game menu (D-042): where it cannot open, the press is the
					// game menu key - it opens the game menu, and goes back or closes it again (#377)
					requests.gameMenu = true;
					status.gameMenuKeys++;
					gameevent::Event("vr.radial.gameMenuKey")("hand", handName(hand))("reason", reason);
				}
				continue;
			}
			menu.hand = frameHands().hands[i].position;
			const Vec3f eye = eyeMiddle();
			bool beforeEyes = false;
			menu.pose = placeRadial(radialAnchor(menu.hand, eye, beforeEyes), eye, g_radial.params);
			// The hand where it opened is the middle - also for a ring moved before the eyes, away from the hand
			menu.origin = radialPlanePoint(menu.pose, menu.hand);
			status.placed = beforeEyes ? "gaze" : "hand";
			menu.selector.reset();
			menu.travel = 0.f;
			menu.openFrames = menu.drawnFrames = menu.submittedFrames = 0;
			status.undrawnLogged = false;
			menu.point = Vec2f(0.f);
			menu.open = true;
			status.open = true;
			status.refused.clear();
			status.opens++;
			status.center = menu.pose.center;
			status.hovered = "none";
			// gazeDeg: how far off the view's middle the ring opened - a ring at a hand held beside the head (the
			// owner's headset run with a panel open, #377) is out of view though it opened
			const Vec3f forward = frameViews().eyes[0].orientation * Vec3f(0.f, 0.f, -1.f);
			const Vec3f toRing = menu.pose.center - eyeMiddle();
			float gazeDeg = 0.f;
			if(glm::length(toRing) > 1e-3f) {
				const float cosine = std::clamp(glm::dot(forward, glm::normalize(toRing)), -1.f, 1.f);
				gazeDeg = glm::degrees(std::acos(cosine));
			}
			gameevent::Event("vr.radial")("hand", handName(hand))("state", "open")
				("x", menu.pose.center.x, 2)("y", menu.pose.center.y, 2)("z", menu.pose.center.z, 2)("gazeDeg", gazeDeg, 0)
				("place", status.placed);
		}

		if(blocked || reason) {
			closeMenu(hand, blocked ? "controls_taken" : reason);
			continue;
		}

		menu.hand = frameHands().hands[i].position;
		menu.point = radialPlanePoint(menu.pose, menu.hand) - menu.origin;
		menu.travel = std::max(menu.travel, glm::length(menu.point));
		menu.openFrames++;
		menu.submittedFrames += radialLayerShown(hand) ? 1 : 0;
		const RadialChoice before = menu.selector.hovered();
		const RadialChoice & hovered = menu.selector.update(menu.point, g_radial.params);
		status.point = menu.point;
		status.halfRing = menu.selector.halfRingOpen();
		status.hovered = radialChoiceName(hovered);
		if(hovered != before) {
			gameevent::Event("vr.radial.hover", 2000)("hand", handName(hand))("item", status.hovered);
		}

		if(!controls.active(event)) {
			// Released: the item under the hand runs; over nothing, an open book, bag, container or note closes (#377)
			const RadialChoice chosen = hovered;
			status.chosen = radialChoiceName(chosen);
			status.result = runChoice(hand, chosen, requests);
			if(chosen.item != RadialItem::None) {
				status.choices++;
			}
			// shown: the menu was in the last submitted frame; travelM: how far the hand went from the middle -
			// a release over nothing tells a menu nobody saw from one nobody moved in (#377)
			gameevent::Event("vr.radial")("hand", handName(hand))("state", "chosen")("item", status.chosen)
				("result", status.result)("shown", radialLayerShown(hand) ? "yes" : "no")("travelM", menu.travel, 2)
				("frames", menu.openFrames)("drawn", menu.drawnFrames)("submitted", menu.submittedFrames);
			menu.open = false;
			status.open = false;
			status.halfRing = false;
			status.hovered = "none";
		}
	}
}

namespace {

const Color4f SegmentColor = Color4f::rgba(0.05f, 0.05f, 0.07f, 0.62f);
const Color4f HoverColor = Color4f::rgba(0.42f, 0.31f, 0.14f, 0.82f);
const Color4f MarkColor = Color4f::rgba(1.f, 0.8f, 0.42f, 0.95f);
const Color4f IconColor = Color4f::rgba(0.93f, 0.92f, 0.88f, 0.95f);
const Color4f DimIconColor = Color4f::rgba(0.93f, 0.92f, 0.88f, 0.4f);
const Color4f HintColor = Color4f::rgba(0.93f, 0.92f, 0.88f, 0.22f);
const Color4f LabelBackColor = Color4f::rgba(0.02f, 0.02f, 0.03f, 0.7f);

//! The ring's plane in the image: meters (x right, y up) -> pixels (x right, y down)
struct Canvas {

	float scale = 1.f;
	Vec2f middle = Vec2f(0.f);

	Vec2f pixel(const Vec2f & meters) const {
		return middle + Vec2f(meters.x, -meters.y) * scale;
	}

	Vec2f polar(float degrees, float radius) const {
		const float a = glm::radians(degrees);
		return pixel(Vec2f(std::cos(a), std::sin(a)) * radius);
	}

};

TexturedVertex vertex(const Vec2f & p, ColorRGBA color) {
	return TexturedVertex(Vec3f(p, 0.f), 1.f, color, Vec2f(0.f));
}

void drawTriangles(const std::vector<TexturedVertex> & vertices) {
	if(!vertices.empty()) {
		GRenderer->ResetTexture(0);
		EERIEDRAWPRIM(Renderer::TriangleList, vertices.data(), vertices.size());
	}
}

//! An annulus sector from \a from to \a to degrees (counterclockwise), radii in meters
void fillSector(const Canvas & canvas, float inner, float outer, float from, float to, const Color4f & color) {
	const ColorRGBA rgba = color.toRGBA();
	const int steps = std::max(2, int(std::ceil((to - from) / 4.f)));
	std::vector<TexturedVertex> vertices;
	vertices.reserve(size_t(steps) * 6);
	for(int i = 0; i < steps; i++) {
		const float a0 = from + (to - from) * float(i) / float(steps);
		const float a1 = from + (to - from) * float(i + 1) / float(steps);
		const Vec2f i0 = canvas.polar(a0, inner);
		const Vec2f o0 = canvas.polar(a0, outer);
		const Vec2f i1 = canvas.polar(a1, inner);
		const Vec2f o1 = canvas.polar(a1, outer);
		for(const Vec2f & p : { i0, o0, o1, i0, o1, i1 }) {
			vertices.push_back(vertex(p, rgba));
		}
	}
	drawTriangles(vertices);
}

void fillDisc(const Canvas & canvas, const Vec2f & center, float radius, const Color4f & color) {
	const ColorRGBA rgba = color.toRGBA();
	const Vec2f c = canvas.pixel(center);
	const float r = radius * canvas.scale;
	std::vector<TexturedVertex> vertices;
	const int steps = 32;
	for(int i = 0; i < steps; i++) {
		const float a0 = glm::two_pi<float>() * float(i) / float(steps);
		const float a1 = glm::two_pi<float>() * float(i + 1) / float(steps);
		vertices.push_back(vertex(c, rgba));
		vertices.push_back(vertex(c + Vec2f(std::cos(a0), std::sin(a0)) * r, rgba));
		vertices.push_back(vertex(c + Vec2f(std::cos(a1), std::sin(a1)) * r, rgba));
	}
	drawTriangles(vertices);
}

/*!
 * A line icon: strokes in a box of -1..1 (y up), drawn \a radius pixels around \a center (pixels) with
 * lines \a width pixels wide
 */
class Icon {

public:

	Icon(const Vec2f & center, float radius, float width, const Color4f & color)
		: m_center(center), m_radius(radius), m_halfWidth(width * 0.5f), m_color(color.toRGBA()) { }

	~Icon() {
		drawTriangles(m_vertices);
	}

	void line(Vec2f a, Vec2f b) {
		a = point(a);
		b = point(b);
		const Vec2f along = b - a;
		const float length = glm::length(along);
		if(length < 1e-3f) {
			return;
		}
		const Vec2f d = along / length * m_halfWidth;
		const Vec2f n(-d.y, d.x);
		// Extended by half the width: joints of a polyline close
		a -= d;
		b += d;
		for(const Vec2f & p : { a + n, a - n, b - n, a + n, b - n, b + n }) {
			m_vertices.push_back(vertex(p, m_color));
		}
	}

	void polyline(std::initializer_list<Vec2f> points, bool closed = false) {
		const Vec2f * previous = nullptr;
		for(const Vec2f & p : points) {
			if(previous) {
				line(*previous, p);
			}
			previous = &p;
		}
		if(closed && points.size() > 2) {
			line(*(points.end() - 1), *points.begin());
		}
	}

	void arc(const Vec2f & center, float radius, float from, float to) {
		const int steps = std::max(3, int(std::ceil(std::abs(to - from) / 15.f)));
		for(int i = 0; i < steps; i++) {
			const float a0 = glm::radians(from + (to - from) * float(i) / float(steps));
			const float a1 = glm::radians(from + (to - from) * float(i + 1) / float(steps));
			line(center + Vec2f(std::cos(a0), std::sin(a0)) * radius, center + Vec2f(std::cos(a1), std::sin(a1)) * radius);
		}
	}

	void circle(const Vec2f & center, float radius) {
		arc(center, radius, 0.f, 360.f);
	}

private:

	Vec2f point(const Vec2f & p) const {
		return m_center + Vec2f(p.x, -p.y) * m_radius;
	}

	Vec2f m_center;
	float m_radius;
	float m_halfWidth;
	ColorRGBA m_color;
	std::vector<TexturedVertex> m_vertices;

};

//! The fixed items' line icons (proposals, D-NEW)
void drawItemIcon(RadialItem item, const Vec2f & center, float radius, const Color4f & color) {

	Icon icon(center, radius, std::max(2.f, radius * 0.14f), color);

	switch(item) {
		case RadialItem::Inventory: {
			// A bag: body, flap, handle
			icon.polyline({ Vec2f(-0.7f, 0.35f), Vec2f(-0.7f, -0.8f), Vec2f(0.7f, -0.8f), Vec2f(0.7f, 0.35f) }, true);
			icon.polyline({ Vec2f(-0.7f, 0.35f), Vec2f(0.f, 0.f), Vec2f(0.7f, 0.35f) });
			icon.arc(Vec2f(0.f, 0.35f), 0.35f, 0.f, 180.f);
			break;
		}
		case RadialItem::GameMenu: {
			for(float y : { 0.5f, 0.f, -0.5f }) {
				icon.line(Vec2f(-0.7f, y), Vec2f(0.7f, y));
			}
			break;
		}
		case RadialItem::Character: {
			icon.circle(Vec2f(0.f, 0.4f), 0.32f);
			icon.arc(Vec2f(0.f, -0.85f), 0.75f, 15.f, 165.f);
			break;
		}
		case RadialItem::Spells: {
			// A five-pointed star
			const float outer = 0.9f;
			const float inner = 0.38f;
			std::array<Vec2f, 10> points;
			for(size_t i = 0; i < points.size(); i++) {
				const float a = glm::radians(90.f + 36.f * float(i));
				points[i] = Vec2f(std::cos(a), std::sin(a)) * ((i % 2 == 0) ? outer : inner);
			}
			for(size_t i = 0; i < points.size(); i++) {
				icon.line(points[i], points[(i + 1) % points.size()]);
			}
			break;
		}
		case RadialItem::Map: {
			icon.polyline({ Vec2f(-0.8f, 0.6f), Vec2f(-0.27f, 0.8f), Vec2f(0.27f, 0.6f), Vec2f(0.8f, 0.8f),
			                Vec2f(0.8f, -0.6f), Vec2f(0.27f, -0.8f), Vec2f(-0.27f, -0.6f), Vec2f(-0.8f, -0.8f) }, true);
			icon.line(Vec2f(-0.27f, 0.8f), Vec2f(-0.27f, -0.6f));
			icon.line(Vec2f(0.27f, 0.6f), Vec2f(0.27f, -0.8f));
			break;
		}
		case RadialItem::Quests: {
			icon.polyline({ Vec2f(-0.6f, 0.85f), Vec2f(0.6f, 0.85f), Vec2f(0.6f, -0.85f), Vec2f(-0.6f, -0.85f) }, true);
			for(float y : { 0.4f, 0.f, -0.4f }) {
				icon.line(Vec2f(-0.32f, y), Vec2f(0.32f, y));
			}
			break;
		}
		case RadialItem::QuickSave:
		case RadialItem::QuickLoad: {
			icon.polyline({ Vec2f(-0.8f, -0.2f), Vec2f(-0.8f, -0.85f), Vec2f(0.8f, -0.85f), Vec2f(0.8f, -0.2f) });
			if(item == RadialItem::QuickSave) {
				icon.line(Vec2f(0.f, 0.85f), Vec2f(0.f, -0.45f));
				icon.polyline({ Vec2f(-0.38f, -0.05f), Vec2f(0.f, -0.45f), Vec2f(0.38f, -0.05f) });
			} else {
				icon.line(Vec2f(0.f, -0.45f), Vec2f(0.f, 0.85f));
				icon.polyline({ Vec2f(-0.38f, 0.45f), Vec2f(0.f, 0.85f), Vec2f(0.38f, 0.45f) });
			}
			break;
		}
		case RadialItem::Recenter: {
			icon.circle(Vec2f(0.f), 0.55f);
			icon.line(Vec2f(0.f, 0.55f), Vec2f(0.f, 0.95f));
			icon.line(Vec2f(0.f, -0.55f), Vec2f(0.f, -0.95f));
			icon.line(Vec2f(0.55f, 0.f), Vec2f(0.95f, 0.f));
			icon.line(Vec2f(-0.55f, 0.f), Vec2f(-0.95f, 0.f));
			icon.circle(Vec2f(0.f), 0.08f);
			break;
		}
		case RadialItem::CancelSpell: {
			icon.circle(Vec2f(0.f), 0.85f);
			icon.line(Vec2f(-0.45f, 0.45f), Vec2f(0.45f, -0.45f));
			icon.line(Vec2f(-0.45f, -0.45f), Vec2f(0.45f, 0.45f));
			break;
		}
		case RadialItem::QuickSlot: {
			// An empty slot: a plus
			icon.line(Vec2f(-0.4f, 0.f), Vec2f(0.4f, 0.f));
			icon.line(Vec2f(0.f, -0.4f), Vec2f(0.f, 0.4f));
			break;
		}
		case RadialItem::None: break;
	}
}

/*
 * The game's own fonts, in the font of its language (item names are localised): the menu's controls font
 * for the name, the in-game font for a count. Not fonts of our own - the font cache must have every font
 * released before it shuts down, and the game's fonts follow a change of the language
 */
//! The name of a fixed item in the game's language where the game names the same thing (its key bindings)
std::string itemLabel(RadialItem item) {
	const char * key = nullptr;
	switch(item) {
		case RadialItem::Inventory: key = "system_menus_options_input_customize_controls_inventory"; break;
		case RadialItem::Character: key = "system_menus_options_input_customize_controls_bookcharsheet"; break;
		case RadialItem::Spells: key = "system_menus_options_input_customize_controls_bookspell"; break;
		case RadialItem::Map: key = "system_menus_options_input_customize_controls_bookmap"; break;
		case RadialItem::Quests: key = "system_menus_options_input_customize_controls_bookquest"; break;
		case RadialItem::QuickSave: key = "system_menus_options_input_customize_controls_quicksave"; break;
		case RadialItem::QuickLoad: key = "system_menus_options_input_customize_controls_quickload"; break;
		case RadialItem::CancelSpell: key = "system_menus_options_input_customize_controls_cancelcurrentspell"; break;
		default: break;
	}
	return std::string(key ? getLocalised(key, radialItemLabel(item)) : std::string_view(radialItemLabel(item)));
}

Font * labelFont() {
	return hFontControls;
}

Font * countFont() {
	return hFontInGame;
}

//! The name of the item under the hand, in one or two lines over a dark disc in the ring's middle
void drawLabel(const Canvas & canvas, const std::string & text) {

	Font * font = labelFont();
	if(text.empty() || !font) {
		return;
	}

	std::vector<std::string> lines = { text };
	const float maxWidth = 2.f * g_radial.params.holeRadius * 1.55f * canvas.scale;
	if(float(font->getTextSize(text).advance()) > maxWidth) {
		// Two lines, split at the space nearest the middle
		size_t best = std::string::npos;
		for(size_t i = text.find(' '); i != std::string::npos; i = text.find(' ', i + 1)) {
			if(best == std::string::npos || std::abs(int(i) - int(text.size() / 2)) < std::abs(int(best) - int(text.size() / 2))) {
				best = i;
			}
		}
		if(best != std::string::npos) {
			lines = { text.substr(0, best), text.substr(best + 1) };
		}
	}

	const float lineHeight = float(font->getLineHeight());
	float y = canvas.middle.y - lineHeight * float(lines.size()) * 0.5f;
	for(const std::string & line : lines) {
		const s32 width = font->getTextSize(line).advance();
		font->draw(Vec2i(s32(canvas.middle.x) - width / 2, s32(y)), line, Color::white);
		y += lineHeight;
	}
}

void drawMenu(Hand hand) {

	const HandMenu & menu = g_radial.menus[size_t(hand)];
	const RadialParams & params = g_radial.params;
	const RadialChoice hovered = menu.selector.hovered();
	const bool halfOpen = menu.selector.halfRingOpen();
	const QuickSlotSet slots = quickSlots(hand);
	const bool dragging = g_draggedEntity && (g_draggedEntity->ioflags & IO_ITEM);

	Canvas canvas;
	canvas.scale = float(RadialImagePixels) / params.imageExtent;
	canvas.middle = Vec2f(float(RadialImagePixels) * 0.5f);

	UseRenderState state(render2D());
	UseTextureState textureState(TextureStage::FilterLinear, TextureStage::WrapClamp);
	GRenderer->SetViewport(Rect(RadialImagePixels, RadialImagePixels));

	const float segmentDegrees = 360.f / float(RadialSegmentCount);
	const float gap = 1.5f;
	const float drawnHole = params.holeRadius * 1.4f;
	const float iconRadius = 0.016f * canvas.scale;
	const float middleRadius = (drawnHole + params.ringRadius) * 0.5f;

	std::string label;

	// The ring: 8 segments, the one under the hand lit, with an arc outside it
	for(size_t segment = 0; segment < RadialSegmentCount; segment++) {
		const float center = segmentCenterDegrees(segment);
		const float from = center - segmentDegrees * 0.5f + gap;
		const float to = center + segmentDegrees * 0.5f - gap;
		const RadialChoice choice = segmentChoice(segment);
		const bool lit = !halfOpen && hovered == choice;
		fillSector(canvas, drawnHole, params.ringRadius, from, to, lit ? HoverColor : SegmentColor);
		if(lit) {
			fillSector(canvas, params.ringRadius + 0.003f, params.ringRadius + 0.009f, from, to, MarkColor);
		}

		const Vec2f iconCenter = canvas.polar(center, middleRadius);
		if(choice.item == RadialItem::Inventory) {
			drawItemIcon(RadialItem::Inventory, iconCenter, iconRadius, IconColor);
			if(lit) {
				label = itemLabel(RadialItem::Inventory);
			}
			continue;
		}

		const std::string & className = slots[choice.slot];
		const SlotItem found = findInInventory(className);
		const bool burning = !className.empty() && player.torch && player.torch->className() == className;
		Entity * shown = found.item ? found.item : (burning ? player.torch : nullptr);
		if(shown && shown->m_icon) {
			const float size = iconRadius * 2.4f;
			const Rectf rect(iconCenter - Vec2f(size * 0.5f), size, size);
			EERIEDrawBitmap(rect, 0.f, shown->m_icon, Color::white);
			if(Font * font = countFont(); font && found.count > 1) {
				const std::string count = std::to_string(found.count);
				const s32 width = font->getTextSize(count).advance();
				font->draw(Vec2i(s32(rect.right) - width / 2, s32(rect.bottom) - font->getLineHeight() / 2), count,
				           Color::white);
			}
		} else {
			drawItemIcon(RadialItem::QuickSlot, iconCenter, iconRadius * (className.empty() ? 0.8f : 1.f), DimIconColor);
		}
		if(shown) {
			g_radial.names[className] = itemName(shown);
		}
		if(lit) {
			// Names in the game's language only: the dragged item, the slot's kind (also when none is left -
			// its icon is then the dim plus); an empty slot shows its plus alone
			if(dragging) {
				label = itemName(g_draggedEntity);
			} else if(!className.empty()) {
				const auto known = g_radial.names.find(className);
				label = (known != g_radial.names.end()) ? known->second : className;
			}
		}
	}

	// The half-ring over the ring: its items when the hand went up there, a faint arc saying it is there
	if(halfOpen) {
		const float iconMiddle = (params.halfInnerRadius + params.halfOuterRadius) * 0.5f;
		for(size_t i = 0; i < HalfRingItemCount; i++) {
			const RadialItem item = halfRingItem(i);
			const float center = halfRingCenterDegrees(i);
			const float from = center - HalfRingItemDegrees * 0.5f + gap * 0.6f;
			const float to = center + HalfRingItemDegrees * 0.5f - gap * 0.6f;
			const bool lit = hovered.item == item;
			fillSector(canvas, params.halfInnerRadius, params.halfOuterRadius, from, to, lit ? HoverColor : SegmentColor);
			if(lit) {
				fillSector(canvas, params.halfOuterRadius + 0.003f, params.halfOuterRadius + 0.009f, from, to, MarkColor);
				label = itemLabel(item);
			}
			drawItemIcon(item, canvas.polar(center, iconMiddle), iconRadius * 0.95f, IconColor);
		}
	} else {
		fillSector(canvas, params.halfInnerRadius, params.halfInnerRadius + 0.004f, 30.f, 150.f, HintColor);
	}

	// The middle: a dark disc with the name of the item under the hand
	fillDisc(canvas, Vec2f(0.f), drawnHole * 0.97f, LabelBackColor);
	drawLabel(canvas, label);

	// Where the hand is, so the player sees what moving it does
	const Vec2f extent(params.imageExtent * 0.48f);
	const Vec2f point = glm::clamp(menu.point, -extent, extent);
	fillDisc(canvas, point, 0.0045f, Color4f::rgba(0.f, 0.f, 0.f, 0.6f));
	fillDisc(canvas, point, 0.003f, Color4f::rgba(1.f, 1.f, 1.f, 0.95f));

	GRenderer->SetViewport(g_preparedCamera.m_viewport);
}

} // anonymous namespace

void drawRadialMenus() {

	RadialStatus & status = g_radial.status;
	for(size_t i = 0; i < HandCount; i++) {
		const Hand hand = Hand(i);
		HandMenu & menu = g_radial.menus[i];
		status.hands[i].shown = radialLayerShown(hand);
		if(!menu.open || !beginRadialImage(hand)) {
			if(menu.open && !status.hands[i].undrawnLogged) {
				// Open but not drawn this frame: the image could not be begun (no stereo frame, an eye in render,
				// no image, its acquire failed) - once per open (#377: the owner's ring missing with a panel)
				status.hands[i].undrawnLogged = true;
				gameevent::Event("vr.radial.undrawn", 200)("hand", handName(hand))("why", radialImageRefusal(hand));
			}
			setRadialLayer(hand, false);
			continue;
		}
		drawMenu(hand);
		endRadialImage(hand);
		setRadialLayer(hand, true, menu.pose.center, menu.pose.orientation, g_radial.params.imageExtent);
		menu.drawnFrames++;
		status.drawn++;
	}
}

const RadialStatus & radialStatus() {
	return g_radial.status;
}

namespace {

std::string_view nextRadialWord(std::string_view & text) {
	const size_t start = text.find_first_not_of(" \t");
	if(start == std::string_view::npos) {
		text = std::string_view();
		return text;
	}
	text.remove_prefix(start);
	const size_t end = text.find_first_of(" \t");
	const std::string_view word = text.substr(0, end);
	text = (end == std::string_view::npos) ? std::string_view() : text.substr(end);
	return word;
}

//! Where the middle of an item is in the ring's plane (meters), nothing for an unknown name
std::optional<Vec2f> itemPoint(std::string_view name, bool & halfRing) {

	const RadialParams & params = g_radial.params;
	const float ringMiddle = (params.holeRadius + params.ringRadius) * 0.5f;
	halfRing = false;

	if(name == "center") {
		return Vec2f(0.f);
	}
	for(size_t segment = 0; segment < RadialSegmentCount; segment++) {
		if(name == radialChoiceName(segmentChoice(segment))) {
			const float a = glm::radians(segmentCenterDegrees(segment));
			return Vec2f(std::cos(a), std::sin(a)) * ringMiddle;
		}
	}
	for(size_t i = 0; i < HalfRingItemCount; i++) {
		RadialChoice choice;
		choice.item = halfRingItem(i);
		if(name == radialChoiceName(choice)) {
			halfRing = true;
			const float a = glm::radians(halfRingCenterDegrees(i));
			return Vec2f(std::cos(a), std::sin(a)) * ((params.halfInnerRadius + params.halfOuterRadius) * 0.5f);
		}
	}
	return std::nullopt;
}

std::string handPosCommand(Hand hand, const HandMenu & menu, const Vec2f & point) {
	const Vec3f x = menu.pose.orientation * Vec3f(1.f, 0.f, 0.f);
	const Vec3f y = menu.pose.orientation * Vec3f(0.f, 1.f, 0.f);
	// In the plane the hand opened the menu in: its offset along the ring's face is kept
	const Vec3f face = menu.pose.orientation * Vec3f(0.f, 0.f, 1.f);
	const float depth = glm::dot(menu.hand - menu.pose.center, face);
	// The ring's middle is where the hand opened it (HandMenu::origin)
	const Vec2f at = point + menu.origin;
	const Vec3f p = menu.pose.center + x * at.x + y * at.y + face * depth;
	std::ostringstream oss;
	oss << "hand " << handName(hand) << " pos " << p.x << ' ' << p.y << ' ' << p.z;
	return oss.str();
}

} // anonymous namespace

std::string radialCommand(std::string_view arguments) {

	std::string_view rest = arguments;
	const std::string_view verb = nextRadialWord(rest);
	if(verb == "place") {
		// place hand|gaze_when_off [<degrees>]: cfg.ini's vr_radial_place / vr_radial_gaze_deg, live (#377)
		const std::string_view mode = nextRadialWord(rest);
		if(mode != "hand" && mode != "gaze_when_off") {
			return "vr-radial place: expected hand or gaze_when_off [<degrees>], got \"" + std::string(mode) + "\"";
		}
		const std::string_view degrees = nextRadialWord(rest);
		if(!degrees.empty()) {
			const std::optional<float> value = util::toFloat(degrees);
			if(!value) {
				return "vr-radial place: expected degrees, got \"" + std::string(degrees) + "\"";
			}
			config.input.vrRadialGazeDeg = std::clamp(*value, 10.f, 120.f);
		}
		config.input.vrRadialPlace = std::string(mode);
		LogInfo << "[vr] radial: menus open at " << config.input.vrRadialPlace << " (off the gaze beyond "
		        << config.input.vrRadialGazeDeg << " deg, test channel)";
		return std::string();
	}
	if(verb != "point") {
		return "vr-radial: expected point <left|right> <item> or place hand|gaze_when_off [<degrees>], got \""
		       + std::string(arguments) + "\"";
	}
	const std::string_view handWord = nextRadialWord(rest);
	if(handWord != "left" && handWord != "right") {
		return "vr-radial point: expected left or right, got \"" + std::string(handWord) + "\"";
	}
	const Hand hand = handWord == "left" ? Hand::Left : Hand::Right;
	const std::string_view item = nextRadialWord(rest);
	bool halfRing = false;
	const std::optional<Vec2f> point = itemPoint(item, halfRing);
	if(!point) {
		return "vr-radial point: unknown item \"" + std::string(item)
		       + "\" (center, inventory, slot1..slot7, game_menu, character, spells, map, quests, quick_save,"
		         " quick_load, recenter, cancel_spell)";
	}
	if(!isSimulated()) {
		return "vr-radial point: needs the simulated headset (--vr-sim)";
	}
	const HandMenu & menu = g_radial.menus[size_t(hand)];
	if(!menu.open) {
		return std::string("vr-radial point: the ") + handName(hand) + " menu is not open";
	}

	// The half-ring opens when the hand goes above the ring and stays open over it: up first, then across
	const RadialParams & params = g_radial.params;
	std::vector<std::string> lines;
	if(halfRing && !menu.selector.halfRingOpen()) {
		lines.push_back(handPosCommand(hand, menu, Vec2f(0.f, (params.halfInnerRadius + params.halfOuterRadius) * 0.5f)));
		lines.push_back("@+3 " + handPosCommand(hand, menu, *point));
	} else if(!halfRing && menu.selector.halfRingOpen()) {
		// Back into the ring first: the middle closes the half-ring
		lines.push_back(handPosCommand(hand, menu, Vec2f(0.f)));
		lines.push_back("@+3 " + handPosCommand(hand, menu, *point));
	} else {
		lines.push_back(handPosCommand(hand, menu, *point));
	}
	for(const std::string & line : lines) {
		const std::string error = simCommand(line);
		if(!error.empty()) {
			return "vr-radial point: " + error;
		}
	}
	return std::string();
}

} // namespace vr
