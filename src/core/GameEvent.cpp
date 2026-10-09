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

#include "core/GameEvent.h"

#include "cinematic/CinematicController.h"
#include "core/Core.h"
#include "core/TimeTypes.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/Equipment.h"
#include "game/Player.h"
#include "gui/Interface.h"
#include "gui/Menu.h"
#include "gui/Note.h"
#include "gui/book/Book.h"
#include "gui/hud/SecondaryInventory.h"
#include "io/log/Logger.h"
#include "platform/Time.h"

namespace gameevent {

namespace {

//! What the UI showed when the last event was logged - compared once per frame
struct UiState {
	bool inventory = false;
	bool inventoryAll = false;
	bool book = false;
	ARX_INTERFACE_BOOK_MODE bookPage = BOOKMODE_STATS;
	const Entity * container = nullptr;
	std::string containerName;
	bool note = false;
	bool combat = false;
};

struct State {
	Limiter limiter;
	GameState state = GameState::Startup;
	UiState ui;
	AreaId loadingArea;
	PlatformInstant loadStart = 0;
	PlatformInstant firstEvent = 0;
};

State g;

void write(const std::string & text) {
	// The only place that writes an event: every line carries the same "GameEvent:<line>" source
	LogInfo << text;
}

void setState(GameState state, AreaId area = AreaId()) {
	if(state == g.state) {
		return;
	}
	Event event("game.state");
	event("to", gameStateName(state))("was", gameStateName(g.state));
	if(area) {
		event("area", u32(area));
	} else if(state == GameState::Gameplay && g_currentArea) {
		event("area", u32(g_currentArea));
	}
	g.state = state;
}

const char * bookPageName(ARX_INTERFACE_BOOK_MODE page) {
	switch(page) {
		case BOOKMODE_STATS:   return "stats";
		case BOOKMODE_SPELLS:  return "spells";
		case BOOKMODE_MINIMAP: return "map";
		case BOOKMODE_QUESTS:  return "quests";
	}
	return "unknown";
}

void updateUi() {

	UiState now;
	now.inventory = (player.Interface & (INTER_INVENTORY | INTER_INVENTORYALL)) != 0;
	now.inventoryAll = (player.Interface & INTER_INVENTORYALL) != 0;
	now.book = (player.Interface & INTER_PLAYERBOOK) != 0;
	now.bookPage = g_playerBook.currentPage();
	now.container = g_secondaryInventoryHud.openContainer();
	now.note = g_note.isOpen();
	now.combat = (player.Interface & INTER_COMBATMODE) != 0;

	UiState & was = g.ui;

	if(now.inventory != was.inventory || (now.inventory && now.inventoryAll != was.inventoryAll)) {
		Event event("ui.inventory");
		event("state", now.inventory ? "open" : "closed");
		if(now.inventory) {
			event("bags", now.inventoryAll ? "all" : "one");
		}
	}

	if(now.book != was.book || (now.book && now.bookPage != was.bookPage)) {
		Event event("ui.book");
		event("state", now.book ? "open" : "closed");
		if(now.book) {
			event("page", bookPageName(now.bookPage));
		}
	}

	if(now.container != was.container) {
		// The entity may be gone by the time its inventory closes: remember its name
		now.containerName = now.container ? entityName(now.container) : std::string();
		if(was.container) {
			Event("ui.container")("state", "closed")("entity", was.containerName);
		}
		if(now.container) {
			Event("ui.container")("state", "open")("entity", now.containerName);
		}
	} else {
		now.containerName = was.containerName;
	}

	if(now.note != was.note) {
		Event("ui.note")("state", now.note ? "open" : "closed");
	}

	if(now.combat != was.combat) {
		Event("player.combat")("state", now.combat ? "on" : "off")
			("weapon", entityName(entities.get(player.equiped[EQUIP_SLOT_WEAPON])));
	}

	was = now;
}

} // anonymous namespace

Event::~Event() {
	// t: seconds since the first event (vr.mode, at startup) - arx.log lines carry no time of their own
	PlatformInstant now = platform::getTime();
	if(g.firstEvent == PlatformInstant(0)) {
		g.firstEvent = now;
	}
	m_line.field("t", toMsi(now - g.firstEvent) / 1000.0, 2);
	// Names never contain a space: the name is the line up to the first field
	std::string_view text = m_line.str();
	std::string_view name = text.substr(0, text.find(' '));
	switch(g.limiter.count(name, m_limit)) {
		case Limiter::Log: {
			write(m_line.str());
			break;
		}
		case Limiter::Notice: {
			// The rest are only counted - status.json events.byName has the true number
			write(Line("event.limit").field("name", name).field("logged", m_limit).field("rest", "counted").str());
			break;
		}
		case Limiter::Drop: break;
	}
}

const char * gameStateName(GameState state) {
	switch(state) {
		case GameState::Startup:   return "STARTUP";
		case GameState::Menu:      return "MENU";
		case GameState::Loading:   return "LOADING";
		case GameState::Gameplay:  return "GAMEPLAY";
		case GameState::Cinematic: return "CINEMATIC";
		case GameState::Exited:    return "EXITED";
	}
	return "UNKNOWN";
}

GameState detectGameState() {
	if(ARXmenu.mode() != Mode_InGame) {
		return GameState::Menu;
	}
	if(!g_currentArea) {
		return GameState::Startup;
	}
	if(isInCinematic()) {
		return GameState::Cinematic;
	}
	return GameState::Gameplay;
}

GameState currentGameState() {
	return g.state;
}

std::string entityName(const Entity * entity) {
	return std::string(idString(entity));
}

void update() {

	if(g.state == GameState::Exited) {
		return;
	}

	setState(detectGameState());

	// UI changes only count while the player plays: the menu hides the interface without closing it
	if(g.state == GameState::Gameplay && entities.player()) {
		updateUi();
	}
}

void levelLoading(AreaId area) {
	g.loadingArea = area;
	g.loadStart = platform::getTime();
	setState(GameState::Loading, area);
	Event("level.load")("area", u32(area));
}

void levelLoaded(AreaId area, bool success) {
	Event event("level.loaded");
	event("area", u32(area))("ok", success);
	if(g.loadStart != PlatformInstant(0) && area == g.loadingArea) {
		event("ms", toMsi(platform::getTime() - g.loadStart));
	}
	g.loadStart = 0;
}

void shutdown() {
	setState(GameState::Exited);
}

size_t total() {
	return g.limiter.total();
}

const std::map<std::string, size_t, std::less<>> & counts() {
	return g.limiter.counts();
}

} // namespace gameevent
