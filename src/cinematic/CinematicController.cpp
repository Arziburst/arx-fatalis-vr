/*
 * Copyright 2011-2022 Arx Libertatis Team (see the AUTHORS file)
 *
 * This file is part of Arx Libertatis.
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

#include "cinematic/CinematicController.h"

#include <string>

#include "core/Application.h"
#include "core/Core.h"
#include "core/GameEvent.h"
#include "core/GameTime.h"

#include "io/log/Logger.h"
#include "io/resource/PakReader.h"
#include "cinematic/CinematicLoad.h"

#include "cinematic/Cinematic.h"
#include "cinematic/CinematicKeyframer.h"
#include "cinematic/CinematicSound.h"

#include "game/Camera.h"
#include "game/Player.h"

#include "gui/Speech.h"

#include "graphics/Renderer.h"
#include "input/Input.h"

#include "window/RenderWindow.h"

enum CinematicState {
	Cinematic_Stopped,
	Cinematic_StartRequested,
	Cinematic_Started
};
static CinematicState PLAY_LOADED_CINEMATIC = Cinematic_Stopped;

static bool CINE_PRELOAD = false;
static std::string WILL_LAUNCH_CINE;
static std::string LAST_LAUNCHED_CINE;
// A skipped cinematic that a script only preloaded (cine -p) and will ask to play (cine play), #124
static bool SKIPPED_CINE_PENDING = false;

Cinematic * ControlCinematique = nullptr; // 2D Cinematic Controller

void cinematicInit() {
	const Vec2i & size = mainApp->getWindow()->getSize();
	ControlCinematique = new Cinematic(size);
}

void cinematicDestroy() {
	delete ControlCinematique, ControlCinematique = nullptr;
}

void cinematicPrepare(std::string_view name, bool preload) {

	WILL_LAUNCH_CINE = name;
	CINE_PRELOAD = preload;
}

/*!
 * Development launches skip every .cin cinematic (#124, owner's decision: straight into the game
 * or the menu, no cutscenes; --cinematics shows them, #92 restores them for players). --intro keeps
 * showing the intro cinematic that the level 10 script plays when a new game starts (#91).
 */
static bool skipCinematic(std::string_view name) {
	if(g_showCinematics) {
		return false;
	}
	return !(g_showIntro && name == "introduction.cin");
}

/*!
 * Ends a skipped cinematic (LAST_LAUNCHED_CINE) through the engine's own skip path - cinematicEnd(),
 * what Esc calls - so the scripts get their cine_end event and the game state is the same as after
 * a real cinematic. The .cin file itself is never loaded: there is nothing to show, and before #93
 * was fixed a loaded cinematic, even one that never played (the level 10 script preloads the intro),
 * made its exit crash likely - measured in #124: 2 of 5 window closes crashed with it loaded.
 * Callers must not touch WILL_LAUNCH_CINE afterwards: a cine_end handler may prepare the next one.
 */
static void endSkippedCinematic() {
	// One line in the event stream (#130); --cinematics shows it
	gameevent::Event("cinematic.skip")("name", LAST_LAUNCHED_CINE)("reason", "development-launch");
	SKIPPED_CINE_PENDING = false;
	cinematicEnd();
}

void cinematicRequestStart() {

	if(SKIPPED_CINE_PENDING && !(ControlCinematique && ControlCinematique->projectload)) {
		// cine play for a preloaded cinematic that was skipped: end it now (#124)
		endSkippedCinematic();
		return;
	}

	PLAY_LOADED_CINEMATIC = Cinematic_StartRequested;
	g_gameTime.pause(GameTime::PauseCinematic);
}

void cinematicKill() {
	SKIPPED_CINE_PENDING = false;
	if(ControlCinematique && ControlCinematique->projectload) {
		ControlCinematique->projectload = false;
		ControlCinematique->OneTimeSceneReInit();
		PLAY_LOADED_CINEMATIC = Cinematic_Stopped;
		g_gameTime.resume(GameTime::PauseCinematic);
		CINE_PRELOAD = false;
	}
}

static Vec3f g_originalCameraPosition;

void cinematicLaunchWaiting() {

	// A cinematic is waiting to be played...
	if(WILL_LAUNCH_CINE.empty()) {
		return;
	}

	// Taken out first: starting (or skipping, #124) it runs scripts that may prepare the next one
	std::string name;
	name.swap(WILL_LAUNCH_CINE);

	LogDebug("LaunchWaitingCine " << CINE_PRELOAD);

	if(g_camera) {
		g_originalCameraPosition = g_camera->m_pos;
	}

	cinematicKill();

	if(skipCinematic(name)) {
		LAST_LAUNCHED_CINE = name;
		if(CINE_PRELOAD) {
			// Only preloaded: the script will ask to play it later (cine play), it ends then
			SKIPPED_CINE_PENDING = true;
			CINE_PRELOAD = false;
		} else {
			endSkippedCinematic();
		}
		return;
	}

	res::path cinematic = res::path("graph/interface/illustrations") / name;

	if(g_resources->getFile(cinematic)) {

		ControlCinematique->OneTimeSceneReInit();

		if(loadCinematic(ControlCinematique, cinematic)) {

			LAST_LAUNCHED_CINE = name;

			if(CINE_PRELOAD) {
				LogDebug("only preloaded cinematic");
				PLAY_LOADED_CINEMATIC = Cinematic_Stopped;
			} else {
				LogDebug("starting cinematic");
				cinematicRequestStart();
			}

		} else {
			LogWarning << "Error loading cinematic " << cinematic;
		}

	} else {
		LogWarning << "Could not find cinematic " << cinematic;
	}

}

bool cinematicIsStopped() {
	return PLAY_LOADED_CINEMATIC == Cinematic_Stopped;
}

bool isInCinematic() {
	return PLAY_LOADED_CINEMATIC != Cinematic_Stopped && ControlCinematique && ControlCinematique->projectload;
}

void cinematicEnd() {
	
	StopSoundKeyFramer();
	cinematicKill();
	
	if(g_camera) {
		arx_assert(isallfinite(g_originalCameraPosition));
		g_camera->m_pos = g_originalCameraPosition;
	}
	
	ARX_SPEECH_Reset();
	SendMsgToAllIO(nullptr, SM_CINE_END, LAST_LAUNCHED_CINE);
	
}

// Manages Currently playing 2D cinematic
void cinematicRender() {

	PlatformDuration diff = g_platformTime.lastFrameDuration();

	if(PLAY_LOADED_CINEMATIC == Cinematic_StartRequested) {
		LogDebug("really starting cinematic now");
		diff = 0;
		PLAY_LOADED_CINEMATIC = Cinematic_Started;
	}

	PlayTrack(ControlCinematique);

	ControlCinematique->Render(diff);

	// end the animation
	if(!ControlCinematique->m_key) {
		cinematicEnd();
	}
	
}
