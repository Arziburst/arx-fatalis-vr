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

#ifndef ARX_CORE_GAMEEVENT_H
#define ARX_CORE_GAMEEVENT_H

#include <map>
#include <string>
#include <string_view>

#include "core/GameEventFormat.h"
#include "graphics/BaseGraphicsTypes.h"

class Entity;

/*!
 * \brief The game event log (#130, diagnostics D2)
 *
 * One \c arx.log line per game and VR state change, always on, so a log alone says what was
 * happening before a problem:
 * \code
 * [I] GameEvent:85       level.loaded area=10 ms=812
 * \endcode
 * Every line comes from one place in GameEvent.cpp, so \c GameEvent: greps the whole stream.
 * The names and fields are listed in docs/vr/TESTING.md ("Game event log"); new features add
 * their own events in the same shape (\c <area>.<what> \c key=value).
 *
 * Rules (F-105): events are state changes, never per frame; a refusal says why with the values;
 * numbers rather than prose. A kind that can repeat without bound gets a limit (first N, then one
 * notice). \c status.json carries the counts (\c events.total, \c events.byName).
 */
namespace gameevent {

//! What the game is doing, as the test channel and the event log report it
enum class GameState {
	Startup,   //!< Before the first level or menu
	Menu,      //!< Main menu, pause menu, credits, character creation
	Loading,   //!< A level is being loaded (set by \ref levelLoading, synchronous within a frame)
	Gameplay,
	Cinematic,
	Exited     //!< Clean shutdown reached
};

const char * gameStateName(GameState state);

//! The state that follows from the menu, level and cinematic now (never Loading or Exited)
GameState detectGameState();

//! The state last logged
GameState currentGameState();

/*!
 * One event, logged when the full expression ends:
 * \code
 * gameevent::Event("spell.cast")("spell", name)("level", 3);
 * \endcode
 * \param limit Log at most this many events of this name (0: no limit); the rest are counted.
 */
class Event {

	Line m_line;
	size_t m_limit;

public:

	explicit Event(std::string_view name, size_t limit = 0) : m_line(name), m_limit(limit) { }
	Event(const Event &) = delete;
	Event & operator=(const Event &) = delete;
	~Event();

	template <typename T>
	Event & operator()(std::string_view key, const T & value) {
		m_line.field(key, value);
		return *this;
	}

	//! A float with \a decimals digits after the point
	Event & operator()(std::string_view key, float value, int decimals) {
		m_line.field(key, value, decimals);
		return *this;
	}

};

//! How an entity is named in events: its id (\c goblin_base_0012), \c player, or \c none
std::string entityName(const Entity * entity);

//! Detect game state and UI changes - once per frame, before anything reads the state
void update();

//! A level starts loading (state \c LOADING)
void levelLoading(AreaId area);

//! A level finished loading, with the time since \ref levelLoading
void levelLoaded(AreaId area, bool success);

//! Log the final state \c EXITED - call before "Clean shutdown"
void shutdown();

//! Events seen so far, logged or not
size_t total();

//! Events seen so far by name, logged or not
const std::map<std::string, size_t, std::less<>> & counts();

} // namespace gameevent

#endif // ARX_CORE_GAMEEVENT_H
