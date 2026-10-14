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
/* Based on:
===========================================================================
ARX FATALIS GPL Source Code
Copyright (C) 1999-2010 Arkane Studios SA, a ZeniMax Media company.

This file is part of the Arx Fatalis GPL Source Code ('Arx Fatalis Source Code').

Arx Fatalis Source Code is free software: you can redistribute it and/or modify it under the terms of the GNU General Public
License as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.

Arx Fatalis Source Code is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with Arx Fatalis Source Code.  If not, see
<http://www.gnu.org/licenses/>.

In addition, the Arx Fatalis Source Code is also subject to certain additional terms. You should have received a copy of these
additional terms immediately following the terms and conditions of the GNU General Public License which accompanied the Arx
Fatalis Source Code. If not, please request a copy in writing from Arkane Studios at the address below.

If you have questions concerning this license or the applicable additional terms, you may contact in writing Arkane Studios, c/o
ZeniMax Media Inc., Suite 120, Rockville, Maryland 20850 USA.
===========================================================================
*/

#include "core/ArxGame.h"

#include <stddef.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <optional>
#include <sstream>
#include <string_view>

#include "ai/PathFinderManager.h"
#include "ai/Paths.h"

#include "animation/Animation.h"
#include "animation/AnimationRender.h"

#include "cinematic/Cinematic.h"
#include "cinematic/CinematicController.h"

#include "core/Benchmark.h"
#include "core/CommandChannel.h"
#include "core/Config.h"
#include "core/Core.h"
#include "core/FpsCounter.h"
#include "core/GameEvent.h"
#include "core/GameTime.h"
#include "core/Localisation.h"
#include "core/SaveGame.h"
#include "core/URLConstants.h"
#include "core/Version.h"

#include "game/Camera.h"
#include "game/Damage.h"
#include "game/EntityManager.h"
#include "game/Equipment.h"
#include "game/Inventory.h"
#include "game/Levels.h"
#include "game/Missile.h"
#include "game/NPC.h"
#include "game/Player.h"
#include "game/Spells.h"
#include "game/effect/ParticleSystems.h"
#include "game/effect/Quake.h"
#include "game/magic/Precast.h"
#include "game/spell/FlyingEye.h"
#include "game/spell/Cheat.h"

#include "graphics/BaseGraphicsTypes.h"
#include "graphics/Color.h"
#include "graphics/Draw.h"
#include "graphics/DrawDebug.h"
#include "graphics/GlobalFog.h"
#include "graphics/GraphicsTypes.h"
#include "graphics/Math.h"
#include "graphics/Raycast.h"
#include "graphics/Vertex.h"
#include "graphics/VertexBuffer.h"
#include "graphics/data/FTL.h"
#include "graphics/data/Mesh.h"
#include "graphics/data/TextureContainer.h"
#include "graphics/effects/Decal.h"
#include "graphics/effects/Fade.h"
#include "graphics/effects/Fog.h"
#include "graphics/effects/LightFlare.h"
#include "graphics/font/Font.h"
#include "graphics/opengl/GLDebug.h"
#include "graphics/particle/ParticleEffects.h"
#include "graphics/particle/ParticleManager.h"
#include "graphics/particle/MagicFlare.h"
#include "graphics/particle/Spark.h"
#include "graphics/texture/TextureStage.h"

#include "gui/CinematicBorder.h"
#include "gui/Console.h"
#include "gui/Cursor.h"
#include "gui/Dragging.h"
#include "gui/Hud.h"
#include "gui/Interface.h"
#include "gui/LoadLevelScreen.h"
#include "gui/Logo.h"
#include "gui/Menu.h"
#include "gui/MenuPublic.h"
#include "gui/MenuWidgets.h"
#include "gui/MiniMap.h"
#include "gui/Notification.h"
#include "gui/Speech.h"
#include "gui/Text.h"
#include "gui/TextManager.h"
#include "gui/debug/DebugHud.h"
#include "gui/debug/DebugHudAudio.h"
#include "gui/debug/DebugHudCulling.h"
#include "gui/Note.h"
#include "gui/hud/PlayerInventory.h"
#include "gui/hud/SecondaryInventory.h"

#include "input/Input.h"
#include "input/Keyboard.h"

#include "math/Angle.h"
#include "math/Random.h"
#include "math/Types.h"
#include "math/Rectangle.h"
#include "math/Vector.h"

#include "physics/Attractors.h"
#include "physics/Collisions.h"

#include "io/fs/FilePath.h"
#include "io/fs/Filesystem.h"
#include "io/fs/SystemPaths.h"
#include "io/resource/PakReader.h"
#include "io/resource/ResourceSetup.h"
#include "io/Screenshot.h"
#include "io/log/CriticalLogger.h"
#include "io/log/Logger.h"

#include "platform/CrashHandler.h"
#include "platform/Dialog.h"
#include "platform/ExitTrace.h"
#include "platform/Platform.h"
#include "platform/Process.h"
#include "platform/ProgramOptions.h"
#include "platform/profiler/Profiler.h"
#include "platform/Time.h"
#include "platform/Thread.h"

#include "scene/ChangeLevel.h"
#include "scene/Interactive.h"
#include "scene/GameSound.h"
#include "scene/Light.h"
#include "scene/LoadLevel.h"
#include "scene/Object.h"
#include "scene/Scene.h"
#include "scene/Tiles.h"

#include "script/Script.h"
#include "script/ScriptEvent.h"

#include "util/String.h"

#include "vr/BodyYaw.h"
#include "vr/EyeProjection.h"
#include "vr/HeadLean.h"
#include "vr/HeadTracking.h"
#include "vr/PerfLog.h"
#include "vr/SceneSubtitles.h"
#include "vr/SceneView.h"
#include "vr/VRBody.h"
#include "vr/VRComfort.h"
#include "vr/VRInput.h"
#include "vr/VRMelee.h"
#include "vr/VRReticle.h"
#include "vr/VRSimulator.h"
#include "vr/VRSystem.h"
#include "vr/VRWeapon.h"
#include "vr/WristHud.h"

#include "Configure.h"

#include "window/RenderWindow.h"

#if ARX_HAVE_SDL2
#include "window/SDL2Window.h"
#endif
#if ARX_HAVE_SDL1
#include "window/SDL1Window.h"
#endif

InfoPanels g_debugInfo = InfoPanelNone;

extern bool START_NEW_QUEST;
extern bool REQUEST_SPEECH_SKIP;
SavegameHandle LOADQUEST_SLOT = SavegameHandle(); // OH NO, ANOTHER GLOBAL! - TEMP PATCH TO CLEAN CODE FLOW
static fs::path g_saveToLoad;

static const PlatformDuration runeDrawPointInterval = 16ms; // ~60fps

extern CircularVertexBuffer<TexturedVertex> * pDynamicVertexBuffer_TLVERTEX; // VB using TLVERTEX format.
extern CircularVertexBuffer<SMY_VERTEX3> * pDynamicVertexBuffer;

bool EXTERNALVIEW = false;
bool SHOW_INGAME_MINIMAP = true;

bool ARX_FLARES_Block = true;

Vec3f PUSH_PLAYER_FORCE;

Vec3f LASTCAMPOS;
Anglef LASTCAMANGLE;

// ArxGame constructor. Sets attributes for the app.
ArxGame::ArxGame()
	: m_wasResized(false)
	, m_gameInitialized(false)
	, m_frameStart(0)
	, m_frameDelta(0)
	, m_vrPacing(false)
{ }

bool ArxGame::initialize() {
	
	// From here on an end of the process outside shutdown() names itself in arx.log (#146)
	platform::initializeExitTrace(fs::getUserDir() / "arx.log", Logger::quickShutdown);
	
	bool init;
	
	init = initConfig();
	if(!init) {
		LogCritical << "Failed to initialize the config subsystem";
		return false;
	}

	// Test command channel and status file (--test-dir): counts log errors from here on
	channel::initialize();

	// arx-perf.log next to arx.log: frame times per second and every hitch with its phases (#327)
	vr::perf::initialize();

	// Decide between VR and flat mode before the window exists: needs no OpenGL yet
	vr::initialize();

	init = initWindow();
	if(!init) {
		return false;
	}

	// The OpenXR session is bound to the OpenGL context created by the window;
	// on failure this logs the reason and the game continues in flat mode
	vr::createSession();
	// One line with the outcome; the lines before it say why when it is flat
	gameevent::Event("vr.mode")("mode", vr::isEnabled() ? "vr" : "flat")("simulated", vr::isSimulated());

	init = initGameData();
	if(!init) {
		return false;
	}
	
	init = initInput();
	if(!init) {
		return false;
	}
	
	init = initSound();
	if(!init) {
		return false;
	}
	
	init = initLocalisation();
	if(!init) {
		LogCritical << "Failed to initialize the localisation subsystem";
		return false;
	}
	
	init = initGame();
	if(!init) {
		LogCritical << "Failed to initialize game";
		return false;
	}
	
	return true;
}

static bool migrateFilenames(fs::path path, bool is_dir) {
	
	std::string_view name = path.filename();
	std::string lowercase = util::toLowercase(name);
	
	bool migrated = true;
	
	if(lowercase != name) {
		
		fs::path dst = path.parent() / lowercase;
		
		LogInfo << "Renaming " << path << " to " << dst.filename();
		
		if(fs::rename(path, dst)) {
			path = dst;
		} else {
			migrated = false;
		}
	}
	
	if(is_dir) {
		for(fs::directory_iterator it(path); !it.end(); ++it) {
			migrated &= migrateFilenames(path / it.name(), it.is_directory());
		}
	}
	
	return migrated;
}

static bool migrateFilenames(const fs::path & configFile) {
	
	LogInfo << "Changing filenames to lowercase...";
	
	static const char * files[] = { "cfg.ini", "cfg_default.ini",
	 "sfx.pak", "loc.pak", "data2.pak", "data.pak", "speech.pak", "loc_default.pak", "speech_default.pak",
	 "save", "editor", "game", "graph", "localisation", "misc", "sfx", "speech" };
	std::set<std::string_view> fileset(std::begin(files), std::end(files));
	
	bool migrated = true;
	
	for(fs::directory_iterator it(fs::getUserDir()); !it.end(); ++it) {
		std::string file = it.name();
		if(fileset.find(util::toLowercase(file)) != fileset.end()) {
			migrated &= migrateFilenames(fs::getUserDir() / file, it.is_directory());
		}
	}
	
	if(!migrated) {
		LogCritical << "Could not rename all files to lowercase, please do so manually and set migration=1 under [misc] in " << configFile;
	}
	
	return migrated;
}

static bool g_forceWindowed = false;
static bool g_configFullscreen = true; // the value from cfg.ini, restored before the config is saved

static void forceWindowed() {
	g_forceWindowed = true;
}
ARX_PROGRAM_OPTION("windowed", "", "Start in a window regardless of the fullscreen setting in cfg.ini",
                   &forceWindowed)

static bool g_forceMute = false;

static void forceMute() {
	g_forceMute = true;
}
ARX_PROGRAM_OPTION("mute", "", "Play no sound in this run; the audio settings in cfg.ini are not changed",
                   &forceMute)

bool ArxGame::initConfig() {

	// Initialize config first, before anything else.
	fs::path configFile = fs::getConfigDir() / "cfg.ini";
	
	config.setOutputFile(configFile);
	
	bool migrated = false;
	if(!fs::exists(configFile)) {
		
		migrated = migrateFilenames(configFile);
		if(!migrated) {
			return false;
		}
		
		fs::path oldConfigFile = fs::getUserDir() / "cfg.ini";
		if(fs::exists(oldConfigFile)) {
			if(!fs::rename(oldConfigFile, configFile)) {
				LogWarning << "Could not move " << oldConfigFile << " to "
				           << configFile;
			} else {
				LogInfo << "Moved " << oldConfigFile << " to " << configFile;
			}
		}
	}
	
	LogInfo << "Using config file " << configFile;
	if(!config.init(configFile)) {
		
		LogWarning << "Could not read config files cfg.ini and cfg_default.ini,"
		           << " using defaults";
		
		// Save a default config file so users have a chance to edit it even if we crash.
		config.save();
	}
	// The scene switches (#29): from cfg.ini at the start; the test channel changes them for the session only
	vr::sceneState().view = vr::parseSceneView(config.video.vrSceneView).value_or(vr::DefaultSceneView);
	vr::sceneState().lookAt = vr::parseSceneLookAt(config.video.vrSceneLookAt).value_or(vr::DefaultSceneLookAt);
	vr::setSubtitlePlacement(vr::parseSubtitlePlacement(config.video.vrSubtitles).value_or(vr::DefaultSubtitlePlacement));
	
	Logger::configure(config.misc.debug);

	g_configFullscreen = config.video.fullscreen;
	if(g_forceWindowed && config.video.fullscreen) {
		// For this run only: the config is saved at exit, so the original value is restored there
		LogInfo << "Starting in a window (--windowed)";
		config.video.fullscreen = false;
	}

	if(g_forceMute) {
		// Applied in the mixer, so config.audio.volume and the rest stay as saved in cfg.ini
		LogInfo << "Audio is muted (--mute): all mixers stay at volume 0, cfg.ini volumes are kept";
		ARX_SOUND_SetMuted(true);
	}

	if(!migrated && config.misc.migration < Config::CaseSensitiveFilenames) {
		migrated = migrateFilenames(configFile);
		if(!migrated) {
			return false;
		}
	}
	if(migrated) {
		config.misc.migration = Config::CaseSensitiveFilenames;
	}
	
	if(!fs::create_directories(fs::getUserDir() / "save")) {
		LogWarning << "Failed to create save directory";
	}
	
	return true;
}

void ArxGame::setWindowSize(bool fullscreen) {
	
	if(fullscreen) {
		
		// Clamp to a sane resolution!
		if(config.video.mode.resolution != Vec2i(0)) {
			config.video.mode.resolution = glm::max(config.video.mode.resolution, Vec2i(640, 480));
		}
		
		getWindow()->setFullscreenMode(config.video.mode);
		
	} else {
		
		// Clamp to a sane window size!
		config.window.size = glm::max(config.window.size, Vec2i(640, 480));
		
		getWindow()->setWindowSize(config.window.size);
		
	}
}

bool ArxGame::initWindow(RenderWindow * window) {
	
	arx_assert(m_MainWindow == nullptr);
	
	m_MainWindow = window;
	
	if(!m_MainWindow->initializeFramework()) {
		m_MainWindow = nullptr;
		return false;
	}
	
	// Register ourself as a listener for this window messages
	m_MainWindow->addListener(this);
	m_MainWindow->getRenderer()->addListener(this);
	
	// Find the next best available fullscreen mode.
	if(config.video.mode.resolution != Vec2i(0)) {
		const RenderWindow::DisplayModes & modes = window->getDisplayModes();
		DisplayMode mode = config.video.mode;
		RenderWindow::DisplayModes::const_iterator i;
		i = std::lower_bound(modes.begin(), modes.end(), mode);
		if(i == modes.end()) {
			mode = *modes.rbegin();
		} else {
			mode = *i;
		}
		if(config.video.mode != mode) {
			if(config.video.mode.resolution != mode.resolution || config.video.mode.refresh != 0) {
				LogWarning << "Fullscreen mode " << config.video.mode << " not supported, using " << mode << " instead";
			}
			config.video.mode = mode;
		}
	}
	
	m_MainWindow->setTitle(arx_name + " " + arx_version);
	m_MainWindow->setMinimizeOnFocusLost(config.window.minimizeOnFocusLost);
	m_MainWindow->setMinTextureUnits(3);
	m_MainWindow->setMaxMSAALevel(config.video.antialiasing ? 8 : 1);
	m_MainWindow->setVSync(benchmark::isEnabled() ? 0 : config.video.vsync);
	
	setWindowSize(config.video.fullscreen);
	
	if(!m_MainWindow->initialize()) {
		m_MainWindow = nullptr;
		return false;
	}
	
	if(GRenderer == nullptr) {
		// We could not initialize all resources in onRendererInit().
		m_MainWindow = nullptr;
		return false;
	}
	
	return true;
}

bool ArxGame::initWindow() {
	
	arx_assert(m_MainWindow == nullptr);
	
	#if ARX_HAVE_SDL2
	if(!m_MainWindow) {
		RenderWindow * window = new SDL2Window;
		if(!initWindow(window)) {
			delete window;
		}
	}
	#endif
	
	#if ARX_HAVE_SDL1
	if(!m_MainWindow) {
		RenderWindow * window = new SDL1Window;
		if(!initWindow(window)) {
			delete window;
		}
	}
	#endif
	
	if(!m_MainWindow) {
		LogCritical << "Graphics initialization failed";
		return false;
	}
	
	return true;
}

bool ArxGame::initInput() {
	
	LogDebug("Input init");
	bool init = ARX_INPUT_Init(m_MainWindow);
	if(!init) {
		LogCritical << "Input initialization failed";
	}
	
	return init;
}

bool ArxGame::initSound() {
	
	LogDebug("Sound init");
	bool init = ARX_SOUND_Init();
	if(!init) {
		LogWarning << "Sound initialization failed";
	}
	
	return true;
}

bool ArxGame::initGameData() {
	
	bool init = addPaks();
	if(!init) {
		LogCritical << "Failed to initialize the game data";
		return false;
	}
	
	savegames.update(true);
	
	return init;
}

TextureContainer * enviro = nullptr;
TextureContainer * ombrignon = nullptr;
TextureContainer * arx_logo_tc = nullptr;


static void LoadSysTextures() {
	
	MagicFlareLoadTextures();

	spellDataInit();

	enviro = TextureContainer::LoadUI("graph/particles/enviro", TextureContainer::NoColorKey);
	
	ARX_INTERFACE_DrawNumberInit();
	initLightFlares();
	ombrignon = TextureContainer::LoadUI("graph/particles/ombrignon");
	arx_logo_tc = TextureContainer::LoadUI("graph/interface/icons/arx_logo_32");
	
	g_hudRoot.init();
	
	// Load book textures and text
	g_bookResouces.init();
	
}

class GameFlow {

public:
	enum Transition {
		FirstLogo,
		SecondLogo,
		LoadingScreen,
		InGame
	};

	static void setTransition(Transition newTransition) {
		s_currentTransition = newTransition;
	}

	static Transition getTransition() {
		return s_currentTransition;
	}

private:
	static Transition s_currentTransition;
};

GameFlow::Transition GameFlow::s_currentTransition = GameFlow::FirstLogo;

static AreaId g_areaToLoad = AreaId(10);
static bool g_initialPlayerCollision = true;

/*
 * A plain launch is the player's (#394), the original game's start: the publisher logos, then the
 * intro - level 10, a scripted scene around the introduction.cin cinematic, which ends in the main
 * menu by itself - and every .cin cinematic in the game. In VR the logos, the intro and the
 * cinematics are shown on a screen in front of the player, and A, B or a trigger skips the one on
 * screen (vr/VRInput.cpp sets g_vrSkipPressed): a logo goes to the next part, the intro straight to
 * the main menu, a cinematic ends through cinematicEnd() as Esc does.
 *
 * Development and test launches pass --dev-launch (run-windows.ps1, smoke-test.ps1, test-pr.ps1 do):
 * straight to the main menu or the game, no logos, no intro, no cutscenes (owner's decisions,
 * 2026-10-08 and #124). --intro brings the logos and the intro back, --cinematics every cinematic. The
 * skip of a development launch lives in the cinematic controller (cinematicLaunchWaiting(),
 * cinematicRequestStart()) and ends the cinematic through the same path as pressing Esc, without
 * loading its file.
 */
bool g_devLaunch = false;
bool g_showIntro = false;
bool g_showCinematics = false;
bool g_vrSkipPressed = false;

static void devLaunch() {
	g_devLaunch = true;
}
ARX_PROGRAM_OPTION("dev-launch", "", "Development launch: skip the publisher logos and every .cin cinematic",
                   &devLaunch)

static void showIntro() {
	g_showIntro = true;
}
ARX_PROGRAM_OPTION("intro", "", "With --dev-launch: show the publisher logos and the intro cinematic anyway",
                   &showIntro)

static void showCinematics() {
	g_showCinematics = true;
}
ARX_PROGRAM_OPTION("cinematics", "", "With --dev-launch: show every .cin cinematic anyway (new-game intro, cutscenes)",
                   &showCinematics)

//! The publisher logo on screen (#394): "fishtank", "arkane", or nullptr
static const char * g_startupLogo = nullptr;

const char * startupLogoName() {
	return g_startupLogo;
}

//! A logo comes on screen, or goes (\a name nullptr); \a how the previous one ended: time, skip, key
static void setStartupLogo(const char * name, const char * how) {
	if(g_startupLogo) {
		gameevent::Event("startup.logo")("name", g_startupLogo)("state", "end")("how", how);
	}
	g_startupLogo = name;
	if(name) {
		gameevent::Event("startup.logo")("name", name)("state", "shown");
	}
}

//! The level 10 intro after the logos plays (#394): until the main menu opens
static bool g_startupIntro = false;

bool startupIntroPlaying() {
	return g_startupIntro;
}

//! The intro is over: \a how "done" (it reached the main menu by itself) or "skip"
static void endStartupIntro(const char * how) {
	g_startupIntro = false;
	gameevent::Event("startup.intro")("state", "end")("how", how);
}

/*!
 * Skip the whole intro (#394, VR): to the main menu as its own end does (the intro's script ends it
 * through the player's death, ARX_PLAYER_Manage_Death()). A playing introduction.cin ends first so
 * its sound and speech stop; the level's scripts then wait behind the menu like after the real end.
 */
static void skipStartupIntro() {
	if(isInCinematic()) {
		cinematicEnd();
	}
	ARX_SPEECH_Reset();
	ARX_MENU_Launch(false);
	endStartupIntro("skip");
}

bool scriptedScenePlaying() {
	return ARXmenu.mode() == Mode_InGame && g_currentArea && !isInCinematic() && !g_startupIntro
	       && cinematicBorder.isActive();
}

void skipScriptedScene(const char * how) {
	gameevent::Event event("scene.skip");
	event("how", how)("area", u32(g_currentArea))("camera", idString(g_cameraEntity));
	// Disabling the skip while fading in or out, and in the scene's first 3 s
	if(FADEDIR || cinematicBorder.elapsedTime() < 3s) {
		event("result", "early");
		return;
	}
	// Every script gets it and the scene's skip handlers end the scene themselves: the prison intro
	// places the hero at its end and gives the controls back. A script that refuses it (any, not only
	// the scene's) keeps the speech playing
	const bool refused = SendMsgToAllIO(nullptr, SM_KEY_PRESSED) == REFUSE;
	if(!refused) {
		REQUEST_SPEECH_SKIP = true;
	}
	event("result", "sent")("refused", refused);
}

//! A skip press of the VR controllers this frame, taken once
static bool takeVrSkip() {
	const bool pressed = g_vrSkipPressed;
	g_vrSkipPressed = false;
	return pressed;
}

static void skipLogo() {
	if(GameFlow::getTransition() != GameFlow::LoadingScreen) {
		GameFlow::setTransition(GameFlow::LoadingScreen);
	}
}
ARX_PROGRAM_OPTION("skiplogo", "", "Skip logos at startup", &skipLogo)

static void startWithNoclip() {
	g_initialPlayerCollision = false;
}
ARX_PROGRAM_OPTION("noclip", "", "Start the game with noclipping enabled", &startWithNoclip)

static void loadLevel(u32 level) {
	g_areaToLoad = AreaId(level);
	skipLogo();
}
ARX_PROGRAM_OPTION_ARG("loadlevel", "", "Load a specific level", &loadLevel, "LEVELID")

static void loadSlot(u32 saveSlot) {
	LOADQUEST_SLOT = SavegameHandle(saveSlot);
	GameFlow::setTransition(GameFlow::InGame);
}
ARX_PROGRAM_OPTION_ARG("loadslot", "", "Load a specific savegame slot", &loadSlot, "SAVESLOT")

static void loadSave(const std::string & saveFile) {
	g_saveToLoad = saveFile;
	GameFlow::setTransition(GameFlow::InGame);
}
ARX_PROGRAM_OPTION_ARG("loadsave", "", "Load a specific savegame file", &loadSave, "SAVEFILE")

//! From the logos to the main menu; \a how the logo on screen ended (nullptr: none was shown)
static void startMainMenu(const char * how) {
	if(how) {
		setStartupLogo(nullptr, how);
	}
	ARX_INTERFACE_KillFISHTANK();
	ARX_INTERFACE_KillARKANE();
	ARXmenu.requestMode(Mode_MainMenu);
	ARX_MENU_Launch(false);
	GameFlow::setTransition(GameFlow::InGame);
}

/*!
 * From the logos to the intro (#394): the original's loading screen loads level 10, whose script plays
 * the intro and ends in the main menu; \a how the ARKANE logo ended
 */
static void startStartupIntro(const char * how) {
	setStartupLogo(nullptr, how);
	GameFlow::setTransition(GameFlow::LoadingScreen);
	g_startupIntro = true;
	gameevent::Event("startup.intro")("state", "start")("area", u32(g_areaToLoad));
}

static bool HandleGameFlowTransitions() {

	const PlatformDuration TRANSITION_DURATION = 3600ms;
	static PlatformInstant TRANSITION_START = 0;

	if(GameFlow::getTransition() == GameFlow::InGame) {
		return false;
	}

	const bool logo = GameFlow::getTransition() == GameFlow::FirstLogo
	                  || GameFlow::getTransition() == GameFlow::SecondLogo;

	if(logo) {
		// No camera is prepared for a logo, so nothing else sets the viewport: in VR the interface is
		// larger than the window and a logo drawn in the window's viewport is cut off (#394)
		GRenderer->SetViewport(g_size);
		if(g_devLaunch && !g_showIntro) {
			// A development launch starts at the main menu, exactly like pressing a key during the logos does
			startMainMenu(nullptr);
			return false;
		}
		// A key goes straight to the main menu as in the original game
		if(GInput->isAnyKeyPressed()) {
			startMainMenu("key");
			return false;
		}
		// The VR skip (#394) skips the logo on screen: the next one follows, after ARKANE the intro
		if(takeVrSkip()) {
			TRANSITION_START = 0;
			if(GameFlow::getTransition() == GameFlow::SecondLogo) {
				startStartupIntro("skip");
				return true;
			}
			setStartupLogo(nullptr, "skip");
			GameFlow::setTransition(GameFlow::SecondLogo);
		}
	}

	if(GameFlow::getTransition() == GameFlow::FirstLogo) {
		
		benchmark::begin(benchmark::Splash);
		
		if(TRANSITION_START == 0) {
			if(!ARX_INTERFACE_InitFISHTANK()) {
				GameFlow::setTransition(GameFlow::SecondLogo);
				return true;
			}
			
			TRANSITION_START = g_platformTime.frameStart();
			setStartupLogo("fishtank", "time");
		}

		ARX_INTERFACE_ShowFISHTANK();
		
		PlatformDuration elapsed = g_platformTime.frameStart() - TRANSITION_START;

		if(elapsed > TRANSITION_DURATION) {
			TRANSITION_START = 0;
			GameFlow::setTransition(GameFlow::SecondLogo);
		}
		
		return true;
	}
	
	if(GameFlow::getTransition() == GameFlow::SecondLogo) {
		
		benchmark::begin(benchmark::Splash);
		
		if(TRANSITION_START == 0) {
			if(!ARX_INTERFACE_InitARKANE()) {
				startStartupIntro("time");
				return true;
			}

			TRANSITION_START = g_platformTime.frameStart();
			setStartupLogo("arkane", "time");
			ARX_SOUND_PlayInterface(g_snd.PLAYER_HEART_BEAT);
		}

		ARX_INTERFACE_ShowARKANE();

		PlatformDuration elapsed = g_platformTime.frameStart() - TRANSITION_START;

		if(elapsed > TRANSITION_DURATION) {
			TRANSITION_START = 0;
			startStartupIntro("time");
		}

		return true;
	}

	if(GameFlow::getTransition() == GameFlow::LoadingScreen) {
		ARX_INTERFACE_KillFISHTANK();
		ARX_INTERFACE_KillARKANE();
		
		benchmark::begin(benchmark::LoadLevel);
		
		ARX_CHANGELEVEL_StartNew();
		
		progressBarReset();
		progressBarSetTotal(108);
		LoadLevelScreen(g_areaToLoad);
		
		DanaeLoadLevel(g_areaToLoad);
		
		USE_PLAYERCOLLISIONS = g_initialPlayerCollision;
		g_initialPlayerCollision = true;
		
		GameFlow::setTransition(GameFlow::InGame);
		return false;
	}

	return false;
}

bool ArxGame::initGame()
{
	// Check if the game will be able to use the current game directory.
	if(!ARX_Changelevel_CurGame_Clear()) {
		LogCritical << "Error accessing current game directory";
		return false;
	}
	
	ScriptEvent::init();
	
	g_fpsCounter.CalcFPS(true);
	
	g_miniMap.mapMarkerInit();
	
	ARX_SPELLS_CancelSpellTarget();
	
	LogDebug("Danae Start");
	
	LogDebug("Project Init");
	
	PUSH_PLAYER_FORCE = Vec3f(0.f);
	ARX_SPECIAL_ATTRACTORS_Reset();
	LogDebug("Attractors Init");
	ARX_SPELLS_Precast_Reset();
	LogDebug("Spell Init");
	
	for(size_t t = 0; t < MAX_GOLD_COINS_VISUALS; t++) {
		GoldCoinsObj[t] = nullptr;
		GoldCoinsTC[t] = nullptr;
	}
	
	LogDebug("LSV Init");
	g_teleportToArea = { };
	TELEPORT_TO_POSITION.clear();
	LogDebug("Mset");
	
	LogDebug("AnimManager Init");
	ARX_SCRIPT_EventStackInit();
	LogDebug("EventStack Init");
	ARX_EQUIPMENT_Init();
	LogDebug("AEQ Init");
	
	ARX_SCRIPT_Timer_ClearAll();
	LogDebug("Timer Init");
	ARX_FOGS_Clear();
	LogDebug("Fogs Init");
	
	EERIE_LIGHT_GlobalInit();
	LogDebug("Lights Init");
	
	LogDebug("Svars Init");
	
	entities.init();
	
	player = ARXCHARACTER();
	ARX_PLAYER_InitPlayer();
	
	notification_ClearAll();
	RemoveQuakeFX();
	
	LogDebug("Launching DANAE");
	
	if(!AdjustUI()) {
		return false;
	}
	
	ARXMenu_Options_Video_SetFogDistance(config.video.fogDistance);
	ARXMenu_Options_Video_SetDetailsQuality(config.video.levelOfDetail);
	ARXMenu_Options_Video_SetGamma(config.video.gamma);
	ARXMenu_Options_Audio_SetMasterVolume(config.audio.volume);
	ARXMenu_Options_Audio_SetSfxVolume(config.audio.sfxVolume);
	ARXMenu_Options_Audio_SetSpeechVolume(config.audio.speechVolume);
	ARXMenu_Options_Audio_SetAmbianceVolume(config.audio.ambianceVolume);
	ARXMenu_Options_Audio_ApplyGameVolumes();
	
	GInput->setMouseSensitivity(config.input.mouseSensitivity);
	GInput->setMouseAcceleration(config.input.mouseAcceleration);
	GInput->setInvertMouseY(config.input.invertMouse);
	GInput->setRawMouseInput(config.input.rawMouseInput);
	
	g_miniMap.firstInit(&player, &entities);
	
	player.m_torchColor = Color3f(1.f, 0.8f, 0.66666f);
	LogDebug("InitializeDanae");
	
	g_tiles = new TileData();
	
	ARX_MISSILES_ClearAll();
	spells.init();

	ARX_SPELLS_ClearAllSymbolDraw();
	ARX_PARTICLES_ClearAll();
	ParticleSparkClear();
	ARX_MAGICAL_FLARES_FirstInit();
	
	LastLoadedScene.clear();
	
	EERIE_PORTAL_Release();
	FreeRoomDistance();
	
	player.size = Vec3f(player.baseRadius(), -player.baseHeight(), player.baseRadius());
	player.desiredangle = player.angle = Anglef(3.f, 268.f, 0.f);
	
	g_playerCamera.angle = player.angle;
	g_playerCamera.m_pos = Vec3f(900.f, player.baseHeight(), 4340.f);
	g_playerCamera.setFov(glm::radians(config.video.fov));
	g_playerCamera.cdepth = 2100.f;
	SetActiveCamera(&g_playerCamera);
	
	LoadSysTextures();
	cursorTexturesInit();
	
	PakReader::ReleaseFlags release = g_resources->getReleaseType();
	if((release & PakReader::Demo) && (release & PakReader::FullGame)) {
		LogWarning << "Mixed demo and full game data files!";
		CrashHandler::setVariable("Data files", "mixed");
	} else if(release & PakReader::Demo) {
		LogInfo << "Initialized Arx Fatalis (demo)";
		CrashHandler::setVariable("Data files", "demo");
	} else if(release & PakReader::FullGame) {
		LogInfo << "Initialized Arx Fatalis (full game)";
		CrashHandler::setVariable("Data files", "full");
	} else {
		LogWarning << "Neither demo nor full game data files loaded!";
		CrashHandler::setVariable("Data files", "unknown");
	}
	
	LogDebug("Before Run...");
	
	cinematicInit();
	
	long old = GLOBAL_EERIETEXTUREFLAG_LOADSCENE_RELEASE;
	GLOBAL_EERIETEXTUREFLAG_LOADSCENE_RELEASE = -1;
	
	gui::NecklaceInit();

	
	drawDebugInitialize();

	eyeball.init();
	LoadSpellModels();
	particleParametersInit();
	
	cameraobj = loadObject("graph/obj3d/interactive/system/camera/camera.teo");
	markerobj = loadObject("graph/obj3d/interactive/system/marker/marker.teo");
	arrowobj = loadObject("graph/obj3d/interactive/items/weapons/arrow/arrow.teo");
	
	for(size_t i = 0; i < MAX_GOLD_COINS_VISUALS; i++) {
		
		std::ostringstream oss;
		
		if(i == 0) {
			oss << "graph/obj3d/interactive/items/jewelry/gold_coin/gold_coin.teo";
		} else {
			oss << "graph/obj3d/interactive/items/jewelry/gold_coin/gold_coin" << (i + 1) << ".teo";
		}
		
		GoldCoinsObj[i] = loadObject(oss.str());
		
		oss.str(std::string());
		
		if(i == 0) {
			oss << "graph/obj3d/interactive/items/jewelry/gold_coin/gold_coin[icon]";
		} else {
			oss << "graph/obj3d/interactive/items/jewelry/gold_coin/gold_coin" << (i + 1) << "[icon]";
		}
		
		GoldCoinsTC[i] = TextureContainer::LoadUI(oss.str());
	}
	
	ARX_PLAYER_LoadHeroAnimsAndMesh();
	
	GLOBAL_EERIETEXTUREFLAG_LOADSCENE_RELEASE = old;
	
	g_playerBook.stats.loadStrings();
	
	m_gameInitialized = true;
	
	return true;
}

#if ARX_PLATFORM != ARX_PLATFORM_WIN32
static void runDataFilesInstaller() {
	static const char * const command[] = { "arx-install-data", "--gui", nullptr };
	if(platform::runHelper(command, true) < 0) {
		std::ostringstream error;
		error << "Could not run `" << command[0] << "`.";
		platform::showErrorDialog(error.str(), "Critical Error - " + arx_name);
	}
}
#endif

bool ArxGame::addPaks() {
	
	arx_assert(!g_resources);
	
	g_resources = new PakReader;
	
	if(!addDefaultResources(g_resources)) {
		
		// Print the search path to the log
		std::ostringstream oss;
		oss << "Searched in these locations:\n";
		std::vector<fs::path> search = fs::getDataSearchPaths();
		for(const fs::path & dir : search) {
			oss << " * " << dir.string() << fs::path::dir_sep << "\n";
		}
		oss << "See " << url::help_install_data << " or `arx --list-dirs` for details.";
		LogInfo << oss.str();
		
		// Try to launch the data file installer on non-Windows systems
		#if ARX_PLATFORM != ARX_PLATFORM_WIN32
		const char * question = "Install the Arx Fatalis data files now?";
		logger::CriticalErrorDialog::setExitQuestion(question, runDataFilesInstaller);
		#endif
		
		// Construct an informative error message about missing files
		oss.str(std::string());
		oss << "Could not load required data files!\n";
		oss << "\nSee " << url::help_get_data << " for help.\n";
		LogCritical << oss.str();
		
		return false;
	}
	
	return true;
}

static void ReleaseSystemObjects() {
	
	if(entities.size() > 0 && entities.player() != nullptr) {
		delete entities.player();
		arx_assert(entities.size() > 0 && entities.player() == nullptr);
	}
	
	eyeball.release();
	ReleaseSpellModels();
	
	cameraobj = { };
	markerobj = { };
	arrowobj = { };
	
	drawDebugRelease();
	
	for(std::unique_ptr<EERIE_3DOBJ> & object : GoldCoinsObj) {
		object = { };
	}
	
}

long EXITING = 0;

void ArxGame::shutdown() {

	// The last second of frames, before the slow parts of the shutdown
	vr::perf::shutdown();

	if(m_gameInitialized)
		shutdownGame();

	// Before the window takes the OpenGL context the VR session is bound to
	vr::shutdown();

	Application::shutdown();

	// game.state to=EXITED, then the final status (cleanShutdown true) for the test channel, if enabled
	gameevent::shutdown();
	channel::shutdown();

	LogInfo << "Clean shutdown";
	platform::markCleanExit();
}

void ArxGame::shutdownGame() {
	
	ARX_Menu_Resources_Release();
	
	mainApp->getWindow()->hide();
	
	Menu2_Close();
	DanaeClearLevel();
	TextureContainer::DeleteAll();
	
	cinematicDestroy();

	if(g_forceWindowed) {
		// --windowed must not leak into cfg.ini
		config.video.fullscreen = g_configFullscreen;
	}
	config.save();

	RoomDrawRelease();
	EXITING = 1;
	TREATZONE_Clear();
	ClearTileLights();
	
	spellDataRelease();
	
	g_particleManager.Clear();
	
	ARX_SOUND_Release();
	
	ARX_PATH_ReleaseAllPath();
	
	ReleaseSystemObjects();
	
	AnchorData_ClearAll();
	
	if(g_tiles) {
		g_tiles->clear();
		FreeRoomDistance();
	}
	
	EERIE_ANIMMANAGER_ClearAll();

	g_renderBatcher.reset();
	
	svar.clear();
	
	ARX_SCRIPT_Timer_ClearAll();
	
	notification_ClearAll();
	ARX_Text_Close();
	
	gui::ReleaseNecklace();
	
	delete g_resources;
	
	ARX_Changelevel_CurGame_Clear();
	
	FreeSnapShot();
	
	ARX_INPUT_Release();
	
	if(getWindow()) {
		EERIE_PATHFINDER_Release();
		ARX_INPUT_Release();
		ARX_SOUND_Release();
	}
	
	ScriptEvent::shutdown();
	
}

void ArxGame::onWindowGotFocus(const Window & /* window */) {
	
	if(GInput) {
		GInput->reset();
	}
	
	if(config.audio.muteOnFocusLost) {
		ARXMenu_Options_Audio_SetMuted(false);
	}
	
}

void ArxGame::onWindowLostFocus(const Window & /* window */) {
	
	// TODO(option-control) add a config option for this
	ARX_INTERFACE_setCombatMode(COMBAT_MODE_OFF);
	TRUE_PLAYER_MOUSELOOK_ON = false;
	PLAYER_MOUSELOOK_ON = false;
	
	// TODO(option-audio) add a config option to disable audio on focus loss
	
	if(config.audio.muteOnFocusLost) {
		ARXMenu_Options_Audio_SetMuted(true);
	}
	
}

void ArxGame::onResizeWindow(const Window & window) {
	
	arx_assert(window.getSize() != Vec2i(0));
	
	// A new window size will require a new backbuffer
	// size, so the 3D structures must be changed accordingly.
	m_wasResized = true;
	
	if(window.isFullScreen()) {
		if(config.video.mode.resolution == Vec2i(0)) {
			LogInfo << "Using fullscreen desktop mode " << window.getDisplayMode();
		} else {
			LogInfo << "Changed fullscreen mode to " << window.getDisplayMode();
			config.video.mode = window.getDisplayMode();
		}
	} else {
		LogInfo << "Changed window size to " << window.getDisplayMode();
		config.window.size = window.getSize();
	}
	
}

void ArxGame::onDestroyWindow(const Window & /* window */) {
	// SDL_QUIT: the close button, Alt+F4, Ctrl+C, or a WM_CLOSE / WM_QUIT sent from outside
	platform::logQuitRequest("the window is being closed (close button, Alt+F4, Ctrl+C or a close message from outside)");
	quit();
}

void ArxGame::onToggleFullscreen(const Window & window) {
	config.video.fullscreen = window.isFullScreen();
}

void ArxGame::onDroppedFile(const Window & /* window */, const fs::path & path) {
	g_saveToLoad = path;
}

/*!
 * \brief Message-processing loop. Idle time is used to render the scene.
 */
void ArxGame::run() {
	
	while(m_RunLoop) {
		
		ARX_PROFILE(Main Loop);
		
		platform::reapZombies();
		
		if(m_MainWindow->isVisible() && !m_MainWindow->isMinimized() && m_bReady) {
			doFrame();
			m_MainWindow->processEvents(/*waitForEvent = */false);
		} else {
			m_MainWindow->processEvents(/*waitForEvent = */true);
		}
		
	}
	
	benchmark::begin(benchmark::Shutdown);
	
}

/*!
 * \brief The frame time a flat frame is judged against in the performance log (#327), microseconds
 *
 * cfg.ini fps_limit when set, else the monitor's refresh rate; 0 when neither is known.
 */
static s64 flatFramePeriodUs(const Window & window) {
	int fps = config.video.fpsLimit > 0 ? config.video.fpsLimit : window.getDisplayMode().refresh;
	return fps > 0 ? 1000000 / fps : 0;
}

/*!
 * \brief The engine's own frame limiter: sleep towards cfg.ini fps_limit (or the monitor's rate)
 */
void ArxGame::limitFrameRate() {
	
	if(!config.video.fpsLimit || benchmark::isEnabled()) {
		return;
	}
	
	PlatformInstant now = platform::getTime();
	
	PlatformDuration lastDuration = now - m_frameStart;
	m_frameStart = now;
	
	int targetFps = config.video.fpsLimit;
	if(targetFps <= 0) {
		targetFps = m_MainWindow->getDisplayMode().refresh;
		if(targetFps <= 0) {
			targetFps = 60;
		}
		if(config.video.vsync) {
			// Give Vsync some headroom in case the refresh rate was rounded down
			targetFps += 1;
		}
	}
	PlatformDuration targetDuration = std::chrono::microseconds(1s) / targetFps;
	
	PlatformDuration min = -targetDuration.value();
	m_frameDelta = arx::clamp(m_frameDelta + targetDuration - lastDuration, min, targetDuration);
	
	if(m_frameDelta > 0) {
		Thread::sleep(m_frameDelta);
	}
	
}
	
/*!
 * \brief One frame clock in VR (D-024, research row F-117)
 *
 * While an OpenXR session exists the headset's xrWaitFrame (vr::beginFrame) paces the game; the
 * window's vsync and the engine's fps limiter would pace it a second time, so both are off, and
 * restored when the session goes away. cfg.ini is not modified.
 */
void ArxGame::applyVRPacing() {
	
	bool vr = vr::isEnabled();
	if(vr == m_vrPacing) {
		return;
	}
	m_vrPacing = vr;
	
	m_MainWindow->setVSync((vr || benchmark::isEnabled()) ? 0 : config.video.vsync);
	
	if(vr) {
		LogInfo << "[vr] pacing: window vsync off and the fps limiter disabled - the headset's xrWaitFrame"
		           " paces the game" << (config.video.fpsLimit > 0
		                                 ? " (cfg.ini fps_limit=" + std::to_string(config.video.fpsLimit)
		                                   + " is ignored in VR; vr_frame_divisor caps the rate)" : "");
	} else {
		LogInfo << "[vr] pacing: flat mode - window vsync " << config.video.vsync << " and fps_limit "
		        << config.video.fpsLimit << " from cfg.ini apply again";
	}
	
	m_frameDelta = PlatformDuration(0);
	m_frameStart = platform::getTime();
	
}
	
/*!
 * \brief The menu the player has open, from the engine's own state (research row F-82), or nullptr
 *
 * In VR the 2D interface of an open menu is shown on the floating panel (#49): the main and pause
 * menu (and the screens behind it: character creation, credits), a note, the book (the map is a
 * page of it), a container and the inventory. Loading screens and cinematics are no menu.
 */
static const char * openMenuName() {
	switch(ARXmenu.mode()) {
		case Mode_MainMenu:          return "mainmenu";
		case Mode_CharacterCreation: return "charactercreation";
		case Mode_Credits:           return "credits";
		case Mode_InGame:            break;
	}
	if(!g_currentArea || !entities.player()) {
		return nullptr;
	}
	if(g_note.isOpen()) {
		return "note";
	}
	if(player.Interface & INTER_PLAYERBOOK) {
		return "book";
	}
	if(g_secondaryInventoryHud.isOpen()) {
		return "container";
	}
	if(player.Interface & (INTER_INVENTORY | INTER_INVENTORYALL)) {
		return "inventory";
	}
	return nullptr;
}

/*!
 * \brief Draws the scene.
 */
void ArxGame::doFrame() {

	// The performance log (#327): the previous frame ends here, this one's phases are timed from now on
	vr::perf::frameStart();

	{
		vr::perf::Scope perfInput(vr::PerfPhase::Input);
		vr::pollEvents();
	}

	// In VR the frame starts when the headset wants it (xrWaitFrame, the only frame clock - D-024);
	// otherwise the engine paces itself as before
	applyVRPacing();
	bool vrFrame;
	{
		vr::perf::Scope perfInput(vr::PerfPhase::Input);
		vrFrame = vr::beginFrame(m_MainWindow->getSize());
	}
	if(!vrFrame) {
		vr::perf::setDisplay(flatFramePeriodUs(*m_MainWindow), 1, false);
		vr::perf::Scope perfLimit(vr::PerfPhase::Limit);
		limitFrameRate();
	}
	// The previous frame is judged now that xrWaitFrame said which display slots it cost
	vr::perf::frameBegun();

	ARX_PROFILE_FUNC();

	updateTime();

	// Controllers -> gameplay events through the control table (#30), synced after xrWaitFrame, and the
	// events -> the engine's actions, mouse buttons and keys (#74), applied by GInput->update() below
	{
		vr::perf::Scope perfInput(vr::PerfPhase::Input);
		vr::updateInput();
	}

	// Game event log: state and UI changes (before the channel, which reports the same state);
	// the OpenXR events were polled at the top of the frame, before vr::beginFrame()
	const gameevent::GameState stateBefore = gameevent::currentGameState();
	gameevent::update();

	// In VR the game is always played in freelook (#394): the hands are the mouse (D-016) and VR has no
	// freelook control. Only a save load sets it (ARX_GAME_Reset); New game and --loadlevel keep the
	// player out of it, and the pause menu turns it off - in cursor mode the engine keeps the inventory
	// open and refuses melee. The inventory the player opened stays open (bInverseInventory)
	if(vr::isEnabled() && stateBefore != gameevent::GameState::Gameplay
	   && gameevent::currentGameState() == gameevent::GameState::Gameplay && !TRUE_PLAYER_MOUSELOOK_ON) {
		TRUE_PLAYER_MOUSELOOK_ON = true;
		gameevent::Event("vr.freelook")("state", "on")("was", gameevent::gameStateName(stateBefore));
	}

	// Crash reports under SteamVR: the overlay or the runtime may replace our exception filter (#132)
	{
		static PlatformInstant lastRearm = 0;
		const PlatformInstant now = platform::getTime();
		if(toUs(now - lastRearm) >= 1000000) {
			lastRearm = now;
			CrashHandler::rearm();
		}
	}

	// Test channel: frame counter, command file, status.json (no-op without --test-dir)
	{
		vr::perf::Scope perfChannel(vr::PerfPhase::Channel);
		channel::update();
	}

	{
		vr::perf::Scope perfInput(vr::PerfPhase::Input);
		updateInput();
	}

	// In VR an open menu goes on the floating panel (#49); the state after this frame's input, the
	// panel is placed when the frame is submitted
	vr::setMenuOpen(openMenuName());

	// The interface's virtual screen follows the window, and in VR the quad's own resolution (#73):
	// entering or leaving a session lays the interface out again like a resized window
	if(vr::interfaceSize(m_MainWindow->getSize()) != g_size.size()) {
		m_wasResized = true;
	}

	if(m_wasResized) {
		LogDebug("was resized");
		m_wasResized = false;
		MenuReInitAll();
		AdjustUI();
		g_hudRoot.recalcScale();
	}

	// Manages Splash Screens if needed
	bool transition;
	{
		vr::perf::Scope perfLoad(vr::PerfPhase::Load);
		transition = HandleGameFlowTransitions();
	}
	if(transition) {
		{
			vr::perf::Scope perfRender(vr::PerfPhase::Render);
			vr::submitFrame(m_MainWindow->getSize());
		}
		vr::perf::Scope perfPresent(vr::PerfPhase::Present);
		m_MainWindow->showFrame();
		return;
	}

	// The intro ends in the main menu by itself (#394); the VR skip ends it there at once, a cinematic
	// through cinematicEnd() and a scripted scene through its skip handlers, as Esc does (scripts get
	// cine_end / key_pressed)
	if(g_startupIntro && ARXmenu.mode() != Mode_InGame) {
		endStartupIntro("done");
	}
	if(takeVrSkip()) {
		if(g_startupIntro) {
			skipStartupIntro();
		} else if(isInCinematic()) {
			cinematicEnd();
		} else if(scriptedScenePlaying()) {
			skipScriptedScene("vr");
		}
	}

	// Clicked on New Quest ? (TODO:need certainly to be moved somewhere else...)
	if(START_NEW_QUEST) {
		LogDebug("start quest");
		vr::perf::Scope perfLoad(vr::PerfPhase::Load);
		DANAE_StartNewQuest();
	}

	// Are we being teleported ?
	if(g_teleportToArea && CHANGE_LEVEL_ICON != NoChangeLevel
	   && (CHANGE_LEVEL_ICON == ChangeLevelNow
	       || config.input.quickLevelTransition == ChangeLevelImmediately
	       || (config.input.quickLevelTransition == JumpToChangeLevel
	           && GInput->actionPressed(CONTROLS_CUST_JUMP)))) {
		// TODO allow binding the same key to multiple actions so that we can have a separate binding for this
		benchmark::begin(benchmark::LoadLevel);
		LogDebug("teleport to " << g_teleportToArea << " " << TELEPORT_TO_POSITION << " " << TELEPORT_TO_ANGLE);
		CHANGE_LEVEL_ICON = NoChangeLevel;
		vr::perf::Scope perfLoad(vr::PerfPhase::Load);
		ARX_CHANGELEVEL_Change(g_teleportToArea, TELEPORT_TO_POSITION, float(TELEPORT_TO_ANGLE));
		g_teleportToArea = { };
		TELEPORT_TO_POSITION.clear();
	}

	if(LOADQUEST_SLOT != SavegameHandle() && LOADQUEST_SLOT.handleData() < long(savegames.size())) {
		vr::perf::Scope perfLoad(vr::PerfPhase::Load);
		ARX_LoadGame(savegames[LOADQUEST_SLOT]);
		LOADQUEST_SLOT = SavegameHandle();
	} else if(LOADQUEST_SLOT != SavegameHandle()) {
		// --loadslot of a save that does not exist: the launch already switched to the game, which then runs
		// with no level at all - a black world with only the HUD, in VR as on the monitor (TRAPS.md, #24).
		// Say so and start at the main menu instead
		LogError << "--loadslot " << LOADQUEST_SLOT.handleData() << ": no such save, " << savegames.size()
		         << " save(s) in the save game dir - starting at the main menu instead of an empty level";
		LOADQUEST_SLOT = SavegameHandle();
		if(!g_currentArea) {
			ARXmenu.requestMode(Mode_MainMenu);
			ARX_MENU_Launch(false);
		}
	}
	
	if(!g_saveToLoad.empty()) {
		if(fs::is_directory(g_saveToLoad)) {
			g_saveToLoad /= SAVEGAME_NAME;
		}
		std::string name;
		float version;
		AreaId area;
		if(!ARX_CHANGELEVEL_GetInfo(g_saveToLoad, name, version, area)) {
			LogError << "Unable to get save file info for " << g_saveToLoad;
		} else {
			SaveGame save;
			save.name = name;
			save.area = area;
			save.savefile = g_saveToLoad;
			vr::perf::Scope perfLoad(vr::PerfPhase::Load);
			ARX_LoadGame(save);
		}
		g_saveToLoad.clear();
	}
	
	if(GInput->actionNowPressed(CONTROLS_CUST_QUICKLOAD)) {
		vr::perf::Scope perfLoad(vr::PerfPhase::Load);
		ARX_QuickLoad();
	}
	
	if(cinematicIsStopped()
	   && !cinematicBorder.isActive()
	   && !BLOCK_PLAYER_CONTROLS
	) {
		
		if(GInput->actionNowPressed(CONTROLS_CUST_QUICKSAVE) && ARXmenu.mode() == Mode_InGame) {
			g_hudRoot.quickSaveIconGui.show();
			vr::perf::Scope perfSave(vr::PerfPhase::Save);
			GRenderer->getSnapshot(savegame_thumbnail, config.interface.thumbnailSize.x, config.interface.thumbnailSize.y);
			ARX_QuickSave();
			g_platformTime.updateFrame();
		}
		
	}
	
	if(g_requestLevelInit) {
		g_requestLevelInit = false;
		vr::perf::Scope perfLoad(vr::PerfPhase::Load);
		levelInit();
	} else {
		cinematicLaunchWaiting();
		{
			// render() updates the game and draws it: its drawing parts are timed as render inside
			vr::perf::Scope perfUpdate(vr::PerfPhase::Update);
			render();
		}
		// In VR: the finished flat frame on a head-locked quad in the headset (no-op in flat mode)
		{
			vr::perf::Scope perfRender(vr::PerfPhase::Render);
			vr::submitFrame(m_MainWindow->getSize());
		}
		// A "shot" from the test channel reads the finished frame before it is shown: in VR the window
		// holds the desktop mirror by now (#271), what a spectator sees
		{
			vr::perf::Scope perfChannel(vr::PerfPhase::Channel);
			channel::afterRender();
		}
		vr::perf::Scope perfPresent(vr::PerfPhase::Present);
		m_MainWindow->showFrame();
	}
}

void ArxGame::updateFirstPersonCamera() {
	
	arx_assert(entities.player());
	
	Entity * io = entities.player();
	AnimLayer & layer1 = io->animlayer[1];
	auto & alist = io->anims;
	
	if(player.m_bowAimRatio != 0.f
	   && layer1.cur_anim != alist[ANIM_MISSILE_STRIKE_PART_1]
	   && layer1.cur_anim != alist[ANIM_MISSILE_STRIKE_PART_2]
	   && layer1.cur_anim != alist[ANIM_MISSILE_STRIKE_CYCLE]) {
		player.m_bowAimRatio -= bowZoomFromDuration(toMsf(g_platformTime.lastFrameDuration()));
		if(player.m_bowAimRatio < 0) {
			player.m_bowAimRatio = 0;
		}
	}
	
	Vec3f targetPos = g_playerCamera.m_pos;
	Anglef targetAngle = g_playerCamera.angle;
	
	if(eyeball.isActive()) {
		
		targetPos = eyeball.getPosition();
		targetAngle = eyeball.getAngle();
		EXTERNALVIEW = true;
		
	} else if(EXTERNALVIEW) {
		
		for(long l = 0; l < 250; l += 10) {
			Vec3f tt = player.pos;
			tt += angleToVectorXZ_180offset(player.angle.getYaw()) * float(l);
			tt += Vec3f(0.f, -50.f, 0.f);
			if(l == 0 || CheckInPoly(tt)) {
				targetPos = tt;
			} else {
				break;
			}
		}
		
		targetAngle = player.angle;
		targetAngle.setPitch(targetAngle.getPitch() + 30.f);
		
	} else {
		
		g_playerCamera.angle = player.angle;
		
		if(VertexId viewVertex = io->obj->fastaccess.view_attach) {

			// The VR body is drawn under the tracked head (#36): its shift is the head's own lean,
			// which the head pose adds to the camera itself - so the engine's eye is the unshifted one
			g_playerCameraStablePos = g_playerCamera.m_pos = io->obj->vertexWorldPositions[viewVertex].v - vr::bodyShift();
			
			if(VertexGroupId viewGroup = getGroupForVertex(io->obj, viewVertex)) {
				AnimLayer animlayer[MAX_ANIM_LAYERS];
				for(size_t i = 0; i < MAX_ANIM_LAYERS; i++) {
					animlayer[i] = io->animlayer[i];
					if(animlayer[i].flags & EA_LOOP) {
						animlayer[i].ctime = AnimationDuration(0);
						animlayer[i].lastframe = -1;
						animlayer[i].currentInterpolation = 0.f;
						animlayer[i].currentFrame = 0;
						animlayer[i].flags |= EA_PAUSED;
					}
				}
				Skeleton skeleton = *io->obj->m_skeleton;
				animateSkeleton(io, animlayer, skeleton);
				g_playerCameraStablePos = skeleton.bones[viewGroup].anim(io->obj->vertexlocal[viewVertex]);
			}

			// The model faces the body, the camera the head (#25): the eye vertex turned with the model about
			// its origin, so take the body's own turn off it again - the body turning never moves the camera
			const float bodyTurn = vr::bodyStatus().offset;
			if(bodyTurn != 0.f) {
				g_playerCamera.m_pos = io->pos + VRotateY(g_playerCamera.m_pos - io->pos, bodyTurn);
				g_playerCameraStablePos = io->pos + VRotateY(g_playerCameraStablePos - io->pos, bodyTurn);
			}

			// View bobbing: the camera follows the animated head of the walking model. Off by cfg.ini in
			// flat mode, always off in VR (#76) - the head-mounted display is the head, so the camera stays
			// at the stable position of the paused animation
			if(!vr::comfort().viewBobbing) {
				g_playerCamera.m_pos = g_playerCameraStablePos;
			}
			
			Vec3f vect(g_playerCamera.m_pos.x - player.pos.x, 0.f, g_playerCamera.m_pos.z - player.pos.z);
			float len = ffsqrt(arx::length2(vect));
			if(len > 46.f) {
				vect *= 46.f / len;
				g_playerCamera.m_pos.x = player.pos.x + vect.x;
				g_playerCamera.m_pos.z = player.pos.z + vect.z;
			}
			
		} else {
			g_playerCameraStablePos = g_playerCamera.m_pos = player.basePosition();
		}

	}
	
	if(EXTERNALVIEW) {
		g_playerCameraStablePos = g_playerCamera.m_pos = (g_playerCamera.m_pos + targetPos) * 0.5f;
		g_playerCamera.angle = interpolate(g_playerCamera.angle, targetAngle, 0.1f);
	}
	
}

void ArxGame::speechControlledCinematic() {
	
	Speech * speech = getCinematicSpeech();
	if(!speech) {
		return;
	}
	
	arx_assert(speech->speaker);
	
	const CinematicSpeech & acs = speech->cine;
	
	float rtime = glm::clamp((g_gameTime.now() - speech->time_creation) / speech->duration, 0.f, 1.f);
	
	switch(acs.type) {
		
		case ARX_CINE_SPEECH_KEEP: {
			arx_assert(isallfinite(acs.pos1));
			g_playerCamera.m_pos = acs.pos1;
			g_playerCamera.angle.setPitch(acs.pos2.x);
			g_playerCamera.angle.setYaw(acs.pos2.y);
			g_playerCamera.angle.setRoll(acs.pos2.z);
			EXTERNALVIEW = true;
			break;
		}
		
		case ARX_CINE_SPEECH_ZOOM: {
			
			arx_assert(isallfinite(acs.pos1));
			
			// Need to compute current values
			float alpha = glm::mix(acs.startangle.getPitch(), acs.endangle.getPitch(), rtime);
			float beta = glm::mix(acs.startangle.getYaw(), acs.endangle.getYaw(), rtime);
			float distance = glm::mix(acs.startpos, acs.endpos, rtime);
			Vec3f targetpos = acs.pos1;
			
			Vec3f vector = angleToVectorXZ(speech->speaker->angle.getYaw() + beta);
			vector.y = std::sin(glm::radians(MAKEANGLE(speech->speaker->angle.getPitch() + alpha)));
			g_playerCamera.m_pos = targetpos + vector * distance;
			
			g_playerCamera.lookAt(targetpos);
			
			EXTERNALVIEW = true;
			
			break;
		}
		
		case ARX_CINE_SPEECH_SIDE_LEFT:
		case ARX_CINE_SPEECH_SIDE: {
			
			if(entities.get(acs.ionum)) {
				
				arx_assert(isallfinite(acs.pos1));
				arx_assert(isallfinite(acs.pos2));
				
				const Vec3f & from = acs.pos1;
				const Vec3f & to = acs.pos2;
				
				Vec3f vect = glm::normalize(to - from);
				Vec3f vect2 = VRotateY(vect, (acs.type == ARX_CINE_SPEECH_SIDE_LEFT) ? -90.f : 90.f);
				
				float distance = glm::mix(acs.m_startdist, acs.m_enddist, rtime);
				float _dist = glm::distance(from, to);
				Vec3f tfrom = from + vect * acs.startpos * (1.0f / 100) * _dist;
				Vec3f tto = from + vect * acs.endpos * (1.0f / 100) * _dist;
				Vec3f targetpos = glm::mix(tfrom, tto, rtime) + Vec3f(0.f, acs.m_heightModifier, 0.f);
				
				g_playerCamera.m_pos = targetpos + vect2 * distance + Vec3f(0.f, acs.m_heightModifier, 0.f);
				
				g_playerCamera.lookAt(targetpos);
				
				EXTERNALVIEW = true;
				
			}
			
			break;
		}
		
		case ARX_CINE_SPEECH_CCCLISTENER_R:
		case ARX_CINE_SPEECH_CCCLISTENER_L:
		case ARX_CINE_SPEECH_CCCTALKER_R:
		case ARX_CINE_SPEECH_CCCTALKER_L: {
			
			// Need to compute current values
			if(entities.get(acs.ionum)) {
				
				arx_assert(isallfinite(acs.pos1));
				arx_assert(isallfinite(acs.pos2));
				
				Vec3f sourcepos = acs.pos1;
				Vec3f targetpos = acs.pos2;
				if(acs.type == ARX_CINE_SPEECH_CCCLISTENER_L || acs.type == ARX_CINE_SPEECH_CCCLISTENER_R) {
					std::swap(sourcepos, targetpos);
				}
				
				float distance = glm::mix(acs.startpos, acs.endpos, rtime) * 0.01f;
				Vec3f vect = sourcepos - targetpos;
				Vec3f vect2 = VRotateY(vect, 90.f);
				vect2 = glm::normalize(vect2);
				Vec3f vect3 = glm::normalize(vect);
				vect = vect * distance + vect3 * 80.f;
				vect2 *= 45.f;
				if(acs.type == ARX_CINE_SPEECH_CCCLISTENER_R || acs.type == ARX_CINE_SPEECH_CCCTALKER_R) {
					vect2 = -vect2;
				}
				
				g_playerCamera.m_pos = vect + targetpos + vect2;
				
				g_playerCamera.lookAt(targetpos);
				
				EXTERNALVIEW = true;
				
			}
			
			break;
		}
		
		case ARX_CINE_SPEECH_NONE: arx_unreachable();
		
	}
	
	LASTCAMPOS = g_playerCamera.m_pos;
	LASTCAMANGLE = g_playerCamera.angle;
	
}

void ArxGame::handlePlayerDeath() {
	
	if(player.lifePool.current <= 0) {
		
		player.DeadTime += g_gameTime.lastFrameDuration();
		float mdist = glm::abs(player.physics.cyl.height) - 60;
		
		float startDistance = 40.f;

		GameDuration startTime = 2s;
		GameDuration endTime = 7s;

		float DeadCameraDistance = startDistance + (mdist - startDistance) * ((player.DeadTime - startTime) / (endTime - startTime));
		
		VertexId id  = entities.player()->obj->fastaccess.view_attach;
		Vec3f targetpos = id ? entities.player()->obj->vertexWorldPositions[id].v : player.pos;
		
		VertexId id2 = getNamedVertex(entities.player()->obj, "chest2leggings");
		Vec3f chest = id2 ? entities.player()->obj->vertexWorldPositions[id2].v : targetpos;
		
		g_playerCamera.m_pos = chest - Vec3f(0.f, DeadCameraDistance, 0.f);
		
		g_playerCamera.lookAt(targetpos);
		
		EXTERNALVIEW = true;
		BLOCK_PLAYER_CONTROLS = true;
		
	}
	
}

/*!
 * The head camera of a stereo frame (#19): its own view with the union of both eyes' frusta as the
 * projection (one cull for both eyes, research row F-163) and the VR near plane (F-39), at the
 * window's size - what the update culls and projects with, and what the 2D interface is drawn with.
 * The eyes themselves are prepared per view in renderStereoViews().
 */
static void prepareStereoHeadCamera(Camera * cam) {
	const float nearDist = vr::NearClipMeters * vr::EyeRigParams().unitsPerMeter;
	PrepareCamera(cam, g_size, cameraWorldToView(*cam), vr::eyeProjection(vr::unionEyeFov(), nearDist, cam->cdepth));
}

/*!
 * \brief Take last frame's head pose off the camera it was put on (#24)
 *
 * The head is a view on top of the engine's camera, not part of its state: the engine's own camera code
 * reads last frame's camera (the external view and the death camera interpolate from it, a conversation
 * keeps its last position), and would otherwise feed the head back into itself every frame.
 */
void ArxGame::restoreEngineCamera() {

	const HeadView view = m_headView;
	m_headView = HeadView();
	// The camera of a camera entity may be gone with its entity: only a camera that is still in use
	Camera * cam = view.camera;
	if(cam && (cam == &g_playerCamera || (g_cameraEntity && cam == &g_cameraEntity->_camdata->cam))) {
		cam->m_pos = view.enginePos;
		cam->angle = view.engineAngle;
	}

}

//! A solid background polygon closer to \a pos than \a radius - exactly, unlike CheckBackgroundInSphere (#26)
static bool backgroundCloserThan(const Vec3f & pos, float radius) {

	const u16 tiles = u16(radius * g_tiles->m_mul.x) + 2;
	for(auto tile : g_tiles->tilesAround(g_tiles->getTile(pos), tiles)) {
		for(const EERIEPOLY & polygon : tile.polygons()) {
			if(polygon.type & (POLY_WATER | POLY_TRANS | POLY_NOCOL)) {
				continue;
			}
			if(vr::distanceToTriangle(pos, polygon.v[0].p, polygon.v[1].p, polygon.v[2].p) < radius) {
				return true;
			}
			if((polygon.type & POLY_QUAD)
			   && vr::distanceToTriangle(pos, polygon.v[1].p, polygon.v[2].p, polygon.v[3].p) < radius) {
				return true;
			}
		}
	}

	return false;
}

/*!
 * Is the tracked head in geometry (#26, D-018)? \return \c behind when a wall is between the camera the
 * engine chose (the character's eye) and the head, \c inside when the head's near plane touches a wall or
 * a fixed entity (a door, a chest) that the engine's own camera does not, nullptr otherwise. Relative to
 * the engine's camera on purpose: where the game itself puts the eye next to a low ceiling or a wall, the
 * head there is no reason to fade.
 */
static const char * headGeometryCheck(const Vec3f & eye, const Vec3f & head) {

	if(closerThan(eye, head, 1.f)) {
		return nullptr;
	}

	if(raycastScene(eye, head, POLY_WATER | POLY_TRANS | POLY_NOCOL)) {
		return "behind";
	}

	// The near plane around both eyes: the clip distance plus half the distance between them
	const float radius = (vr::NearClipMeters + 0.035f) * vr::HeadCameraParams().unitsPerMeter;
	if(backgroundCloserThan(head, radius) && !backgroundCloserThan(eye, radius)) {
		return "inside";
	}
	const CASFlags fixedOnly = CAS_NO_NPC_COL | CAS_NO_ITEM_COL | CAS_NO_DEAD_COL | CAS_NO_BACKGROUND_COL;
	if(CheckAnythingInSphere(Sphere(head, radius), entities.player(), fixedOnly)
	   && !CheckAnythingInSphere(Sphere(eye, radius), entities.player(), fixedOnly)) {
		return "inside";
	}

	return nullptr;
}

//! The hero's eye this frame, where the first-person camera put it before a scene's camera took over (#29)
static Vec3f g_sceneFirstPos(0.f);
//! The stick's turn so far at the last two scene frames: the frame after a stick turn has no yaw step (#29)
static std::array<float, 2> g_sceneTurned = { 0.f, 0.f };

//! \return the floor's y under \a pos (y down), \a found false when there is none
static float floorBelow(const Vec3f & pos, bool & found) {
	float floorY = 0.f;
	found = CheckInPoly(pos, &floorY) != nullptr;
	return floorY;
}

//! How far from \a from towards \a to before the level's walls and floors (#29)
static float freeDistance(const Vec3f & from, const Vec3f & to) {
	const RaycastResult hit = raycastScene(from, to, POLY_WATER | POLY_TRANS | POLY_NOCOL);
	return hit ? glm::distance(from, hit.pos) : glm::distance(from, to);
}

/*!
 * The NPC a scene's shot looks at (#29): the camera entity's target, else the line's speaker or the one its
 * camera turns to, else the living NPC nearest to the hero within 10 m; nullptr for none
 */
static const Entity * sceneSubject(const Speech * speech) {

	const Entity * hero = entities.player();
	auto npc = [hero](const Entity * entity) -> const Entity * {
		return (entity && entity != hero && (entity->ioflags & IO_NPC) && !IsDeadNPC(*entity)) ? entity : nullptr;
	};

	if(g_cameraEntity) {
		if(const Entity * target = npc(entities.get(g_cameraEntity->targetinfo))) {
			return target;
		}
	}
	if(speech) {
		if(const Entity * speaker = npc(speech->speaker)) {
			return speaker;
		}
		if(const Entity * listener = npc(entities.get(speech->cine.ionum))) {
			return listener;
		}
	}

	const Entity * nearest = nullptr;
	float nearestDistance = 1000.f;
	for(const Entity & entity : entities.inScene(IO_NPC)) {
		const float distance = glm::distance(entity.pos, player.basePosition());
		if(npc(&entity) && distance < nearestDistance) {
			nearest = &entity;
			nearestDistance = distance;
		}
	}
	return nearest;
}

/*!
 * The observer's point for this scene (#29, b): beside the line between the hero's head and the head of the
 * NPC the shot looks at, at the player's real eye height above the floor there (the character's eye when the
 * room's floor is unknown)
 */
static vr::ObserverPlace placeSceneObserver(const Speech * speech, const Vec3f & gameCamera) {

	vr::ObserverInput in;
	in.heroHead = player.pos;
	in.otherHead = in.heroHead;
	const Entity * subject = sceneSubject(speech);
	if(subject) {
		const float height = subject->physics.cyl.height < 0.f ? subject->physics.cyl.height * 0.9f : -160.f;
		in.otherHead = subject->pos + Vec3f(0.f, height, 0.f);
	}
	in.gameCamera = gameCamera;
	in.heroYaw = player.angle.getYaw();
	const float realEye = vr::headStatus().referenceAboveFloor;
	in.eyeHeight = realEye >= 0.5f ? realEye * vr::UnitsPerMeter : -player.baseHeight();

	vr::ObserverWorld world;
	world.freeDistance = freeDistance;
	world.floorBelow = [](const Vec3f & pos) -> std::optional<float> {
		bool found = false;
		const float floorY = floorBelow(pos, found);
		return found ? std::optional<float>(floorY) : std::nullopt;
	};

	const vr::ObserverPlace place = vr::placeObserver(in, world);
	LogInfo << "[vr] scene observer at " << place.pos.x << ' ' << place.pos.y << ' ' << place.pos.z
	        << " yaw " << place.yaw << ", watching the hero and " << gameevent::entityName(subject)
	        << ", eye " << in.eyeHeight << " units above the floor";
	return place;
}

/*!
 * The scene view of this VR frame (#29): which scripted scene owns the game's camera, what the eyes get
 * instead (vr/SceneView.h), the scene's events, a pending playerlookat turned at black
 */
static vr::SceneOutput updateSceneView(const Camera & engineCamera) {

	vr::SceneState & state = vr::sceneState();

	vr::SceneInput in;
	const Speech * speech = getCinematicSpeech();
	const u64 speechLine = speech ? (u64(reinterpret_cast<uintptr_t>(speech)) ^ (u64(toMsi(speech->time_creation)) << 16))
	                              : 0;
	if(g_cameraEntity) {
		in.kind = vr::SceneKind::Entity;
		in.shot = u64(reinterpret_cast<uintptr_t>(g_cameraEntity));
	} else if(speech) {
		in.kind = vr::SceneKind::Speech;
		in.shot = speechLine;
	}
	in.active = in.kind != vr::SceneKind::None || cinematicBorder.isActive();
	in.setting = state.view;
	in.enginePos = engineCamera.m_pos;
	in.engineYaw = engineCamera.angle.getYaw();
	in.engineFloorY = floorBelow(in.enginePos, in.engineFloor);
	in.firstPos = g_sceneFirstPos;
	floorBelow(in.firstPos, in.firstOnFloor);
	in.playerYaw = player.angle.getYaw();
	const vr::HeadStatus & head = vr::headStatus();
	in.headYaw = head.referenced ? vr::wrapSignedDegrees(head.head.getYaw() - head.reference.yaw) : 0.f;
	in.seconds = toMsf(g_platformTime.lastFrameDuration()) / 1000.f;
	// The hybrid: a camera entity's shot of a far place, or of the hero's body played by a script, on the screen
	if(in.kind == vr::SceneKind::Entity && in.setting == vr::SceneViewSetting::Hybrid) {
		vr::HybridShot shot;
		shot.camera = engineCamera.m_pos;
		shot.heroHead = player.pos;
		shot.lineOfSight = !raycastScene(shot.heroHead, shot.camera, POLY_WATER | POLY_TRANS | POLY_NOCOL);
		const Entity * hero = entities.player();
		shot.heroScripted = hero && (hero->animlayer[0].flags & EA_FORCEPLAY);
		in.hybridScreen = vr::hybridWantsScreen(shot);
	}
	const Vec3f gameCamera = engineCamera.m_pos;
	in.placeObserver = [speech, gameCamera]() { return placeSceneObserver(speech, gameCamera); };

	const vr::SceneOutput out = state.director.update(in);
	state.active = in.active;
	state.kind = in.kind;

	// playerlookat with the fade (#29): the hero turns now, at black
	if(out.applyLookAt && state.lookAtPending) {
		state.lookAtPending = false;
		player.desiredangle = player.angle = state.lookAtAngle;
		state.meter.lookAtFaded();
	}

	const char * view = vr::sceneViewName(in.setting);
	if(out.ended) {
		gameevent::Event("vr.scene", 200)("state", "end")("kind", vr::sceneKindName(out.endedKind))("view", view)
			("shown", vr::sceneShownName(out.shown));
	}
	if(out.started) {
		gameevent::Event("vr.scene", 200)("state", "start")("kind", vr::sceneKindName(out.kind))("view", view)
			("shown", vr::sceneShownName(out.shown));
	}
	if(out.cut) {
		gameevent::Event("vr.scene.cut", 500)("faded", out.fade >= vr::BehindFade ? "yes" : "no")
			("reason", out.cutReason);
	}

	return out;
}

//! One VR frame for the scene meter (#29): the camera the head goes on, \a pos and \a yaw
static void meterSceneFrame(const vr::SceneOutput & scene, const Vec3f & pos, float yaw) {

	vr::SceneState & state = vr::sceneState();

	vr::SceneMeterFrame frame;
	frame.active = state.active;
	frame.shown = scene.shown;
	frame.fade = scene.fade;
	frame.pos = pos;
	frame.yaw = yaw;
	bool floorFound = false;
	const float floorY = floorBelow(pos, floorFound);
	frame.aboveFloor = floorFound ? (floorY - pos.y) / vr::UnitsPerMeter : 0.f;
	frame.shotChanged = scene.cut;
	// The stick's turn reaches the hero's yaw a frame later: this frame and the next are the player's own
	const float turned = vr::turnState().turned();
	frame.ownTurn = turned != g_sceneTurned[0] || turned != g_sceneTurned[1];
	g_sceneTurned[1] = g_sceneTurned[0];
	g_sceneTurned[0] = turned;

	// Diagnostics: where a seen scene frame first puts the eyes too low (the meter keeps only the minimum)
	static bool s_low = false;
	if(state.meter.frames() == 0) {
		s_low = false;
	}
	const bool seen = scene.shown != vr::SceneShown::None && scene.shown != vr::SceneShown::Screen
	                  && scene.fade < vr::BehindFade;
	const bool low = seen && frame.aboveFloor < vr::GhostMinAboveFloor / vr::UnitsPerMeter;
	if(low && !s_low) {
		const Entity * hero = entities.player();
		LogInfo << "[vr] scene meter: the eyes' camera " << frame.aboveFloor << " m above the floor"
		        << (floorFound ? "" : " (no floor under it)") << " at " << pos.x << ' ' << pos.y << ' ' << pos.z
		        << ", shown " << vr::sceneShownName(scene.shown) << ", kind " << vr::sceneKindName(state.kind)
		        << ", the hero's body " << ((hero && (hero->animlayer[0].flags & EA_FORCEPLAY)) ? "scripted" : "free");
	}
	s_low = low;

	state.meter.add(frame);

}

void ArxGame::updateActiveCamera() {

	ARX_PROFILE_FUNC();

	Camera * cam = nullptr;
	if(g_cameraEntity) {
		cam = &g_cameraEntity->_camdata->cam;
		if(cam->focal < 100.f) {
			cam->focal = 350.f;
		}
		EXTERNALVIEW = true;
	} else {
		cam = &g_playerCamera;
	}

	// The headset is the head (#24, D-018): whatever camera the engine chose this frame - first person, a
	// conversation's camera, the death camera - takes the head's orientation and movement on top, after
	// everything the engine did to it (F-31); a camera that ignored the head would glue the world to the
	// face. The stable position moves with the player's camera, so drag rays start where the player looks
	// from. restoreEngineCamera() takes it off again before the engine updates its cameras next frame
	Vec3f enginePos = cam->m_pos;
	Anglef engineAngle = cam->angle;
	Vec3f restorePos = enginePos;
	Anglef restoreAngle = engineAngle;

	// Scripted scenes in VR (#29): the scene view decides what the eyes get - the hero's head, a held shot of the
	// game's camera - and the head goes on the player's camera placed there, turned with the hero (the body's
	// yaw stays the hero's) and then by the shot's yaw. The game's own scene cameras run on underneath; the
	// player's camera gets the game's pose back next frame. The hybrid's screen shows the game's own camera,
	// drawn flat on a quad fixed in the room like the intro (#394): neither placed nor head-driven
	vr::SceneOutput scene;
	const bool sceneFrame = vr::isEnabled() && !g_startupIntro;
	if(sceneFrame) {
		scene = updateSceneView(*cam);
		if(scene.drive && scene.shown != vr::SceneShown::Screen) {
			restorePos = g_playerCamera.m_pos;
			restoreAngle = g_playerCamera.angle;
			cam = &g_playerCamera;
			cam->m_pos = scene.pos;
			cam->angle = Anglef(0.f, player.angle.getYaw(), 0.f);
			enginePos = cam->m_pos;
			engineAngle = cam->angle;
			EXTERNALVIEW = vr::showsHeroOutside(scene.shown);
		}
	} else if(!vr::isEnabled()) {
		vr::SceneState & state = vr::sceneState();
		state.director.reset();
		state.active = false;
		state.kind = vr::SceneKind::None;
		state.lookAtPending = false;
	}
	const bool sceneScreen = sceneFrame && scene.shown == vr::SceneShown::Screen;
	vr::setSceneScreen(sceneScreen);

	const char * headInGeometry = nullptr;
	if(cam == &g_playerCamera && !EXTERNALVIEW) {
		// The character's eye height, which the head at its reference becomes (#27) - evidence for the scale;
		// and whether it is the standing eye: crouching, getting up and levitating lower or raise it (#100)
		const Entity * io = entities.player();
		const bool crouching = ((player.m_currentMovement | player.m_lastMovement) & PLAYER_CROUCH)
		                       || (io && io->animlayer[0].cur_anim
		                           && io->animlayer[0].cur_anim == io->anims[ANIM_CROUCH_END]);
		vr::setCharacterEyeHeight(player.basePosition().y - g_playerCameraStablePos.y,
		                          !crouching && !player.levitate);
	}
	// The intro (#394) and the hybrid's screen (#29) are a film on the screen in front of the player: their
	// scripted camera is not the head
	if(!g_startupIntro && !sceneScreen && vr::applyHeadPose(cam->m_pos, cam->angle, engineAngle.getYaw())) {
		m_headView = HeadView{ cam, restorePos, restoreAngle };
		if(scene.drive && scene.yawDelta != 0.f) {
			cam->m_pos = enginePos + vr::turnAboutVertical(cam->m_pos - enginePos, scene.yawDelta);
			cam->angle.setYaw(MAKEANGLE(cam->angle.getYaw() + scene.yawDelta));
		}
		if(cam == &g_playerCamera) {
			g_playerCameraStablePos += cam->m_pos - enginePos;
		}
		headInGeometry = headGeometryCheck(enginePos, cam->m_pos);
	} else if(scene.drive && !sceneScreen) {
		m_headView = HeadView{ cam, restorePos, restoreAngle };
		cam->angle.setYaw(MAKEANGLE(cam->angle.getYaw() + scene.yawDelta));
	}
	vr::updateWallFade(headInGeometry, toMsf(g_platformTime.lastFrameDuration()) / 1000.f);

	if(sceneFrame) {
		// On the screen the eyes' camera is where the cut to it held it, not the game's camera on the screen
		if(sceneScreen) {
			meterSceneFrame(scene, scene.pos, MAKEANGLE(player.angle.getYaw() + scene.yawDelta));
		} else {
			meterSceneFrame(scene, enginePos, MAKEANGLE(engineAngle.getYaw() + scene.yawDelta));
		}
		// The hero shown in third person (a ghost shot, a camera of the game's own) has the engine's arms (#29)
		vr::sceneState().armsIk = !EXTERNALVIEW;
	}

	ManageQuakeFX(cam);

	if(vr::stereoActive()) {
		prepareStereoHeadCamera(cam);
	} else {
		PrepareCamera(cam, g_size);
	}

	// Where the hands' rays start next frame, and how their hits become the mouse (#37)
	vr::noteHeadCamera(cam == &g_playerCamera && !EXTERNALVIEW && m_headView.camera == cam, cam->m_pos,
	                   engineAngle.getYaw(), g_preparedCamera.m_worldToScreen);

}

void ArxGame::updateTime() {
	
	g_platformTime.updateFrame();
	
	if(g_requestLevelInit) {
		g_platformTime.overrideFrameDuration(0);
	}
	
	g_gameTime.update(g_platformTime.lastFrameDuration());
	
	g_framedelay = toMsf(g_gameTime.lastFrameDuration());
	
}

void ArxGame::updateInput() {

	// Update input
	GInput->update(toMsf(g_platformTime.lastFrameDuration()));
	
	if(ARXmenu.mode() == Mode_InGame) {
		
		// Handle double clicks.
		const ActionKey & button = config.actions[CONTROLS_CUST_ACTION];
		if((button.key[0] != ActionKey::UNUSED && (button.key[0] & Mouse::ButtonBase) && GInput->getMouseButtonDoubleClick(button.key[0]))
		   || (button.key[1] != ActionKey::UNUSED && (button.key[1] & Mouse::ButtonBase) && GInput->getMouseButtonDoubleClick(button.key[1]))) {
			EERIEMouseButton |= 4;
			EERIEMouseButton &= ~1;
		}
		
		if(GInput->actionNowPressed(CONTROLS_CUST_ACTION)) {
			if(EERIEMouseButton & 4) {
				EERIEMouseButton &= ~1;
			} else {
				EERIEMouseButton |= 1;
			}
		}
		if(GInput->actionNowReleased(CONTROLS_CUST_ACTION)) {
			EERIEMouseButton &= ~1;
			EERIEMouseButton &= ~4;
		}
		
		if(GInput->actionNowPressed(CONTROLS_CUST_USE)) {
			EERIEMouseButton |= 2;
		}
		if(GInput->actionNowReleased(CONTROLS_CUST_USE)) {
			EERIEMouseButton &= ~2;
		}
		
	} else {
		
		EERIEMouseButton = 0;
		
		if(GInput->getMouseButtonRepeat(Mouse::Button_0)) {
			EERIEMouseButton |= 1;
		}
		
		if(GInput->getMouseButtonRepeat(Mouse::Button_1)) {
			EERIEMouseButton |= 2;
		}
		
	}

	if(GInput->actionNowPressed(CONTROLS_CUST_TOGGLE_FULLSCREEN)) {
		setWindowSize(!getWindow()->isFullScreen());
	}

	if(GInput->isKeyPressedNowPressed(Keyboard::Key_F12)) {
		/*
		EERIE_PORTAL_ReleaseOnlyVertexBuffer();
		ComputePortalVertexBuffer();
		*/
		
		profiler::flush();
	}

	if(GInput->isKeyPressedNowPressed(Keyboard::Key_F11)) {

		g_debugInfo = static_cast<InfoPanels>(g_debugInfo + 1);

		if(g_debugInfo == InfoPanelEnumSize)
			g_debugInfo = InfoPanelNone;
	}
	
	if(GInput->isKeyPressedNowPressed(Keyboard::Key_F10)) {
		GetSnapShot();
	}

	if(GInput->actionNowPressed(CONTROLS_CUST_DEBUG)) {
		drawDebugCycleViews();
	}
	
	g_console.update();
	
#ifdef ARX_DEBUG
	debug_keysUpdate();
	
	if(GInput->isKeyPressedNowPressed(Keyboard::Key_Pause)) {
		if(g_gameTime.isPaused() & GameTime::PauseUser) {
			g_gameTime.resume(GameTime::PauseUser);
		} else {
			g_gameTime.pause(GameTime::PauseUser);
		}
	}
#endif
	
	m_MainWindow->allowScreensaver(!m_MainWindow->isFullScreen() && ARXmenu.mode() == Mode_MainMenu);
	
}

extern int iHighLight;

void ArxGame::updateLevel() {

	arx_assert(entities.player());
	
	ARX_PROFILE_FUNC();
	
	g_renderBatcher.clear();
	
	if(!player.m_paralysed) {
		manageEditorControls();

		// In VR a scripted scene blocks walking even where its script leaves the controls on (#29, owner:
		// turning allowed in dialogues, walking blocked) - the stick turn is in manageKeyMouse()
		if(!BLOCK_PLAYER_CONTROLS && !(vr::isEnabled() && vr::sceneState().inScene())) {
			managePlayerControls();
		}
	}
	
	{
		ARX_PROFILE("Entity preprocessing");
		
		for(Entity & entity : entities) {
			
			if(entity.ignition > 0.f || (entity.ioflags & IO_FIERY)) {
				ManageIgnition(entity);
			}
			
			// Highlight entity
			if(&entity == FlyingOverIO && !(entity.ioflags & IO_NPC)) {
				entity.highlightColor = Color3f::gray(float(iHighLight));
			} else {
				entity.highlightColor = Color3f::black;
			}
			
			Cedric_ApplyLightingFirstPartRefactor(entity);
			
			float speedModifier = 0.f;
			
			if(entity == *entities.player()) {
				if(cur_mr == CHEAT_ENABLED) {
					speedModifier += 0.5f;
				}
				if(cur_rf == CHEAT_ENABLED) {
					speedModifier += 1.5f;
				}
			}
			
			speedModifier += spells.getTotalSpellCasterLevelOnTarget(entity.index(), SPELL_SPEED) * 0.1f;
			speedModifier -= spells.getTotalSpellCasterLevelOnTarget(entity.index(), SPELL_SLOW_DOWN) * 0.05f;
			entity.speed_modif = speedModifier;
			
		}
		
	}
	
	ARX_PLAYER_Manage_Movement();

	ARX_PLAYER_Manage_Visual();

	g_miniMap.setActiveBackground(g_tiles);
	g_miniMap.validatePlayerPos(g_currentArea, BLOCK_PLAYER_CONTROLS, g_playerBook.currentPage());


	if(entities.player()->animlayer[0].cur_anim) {
		ManageNONCombatModeAnimations();
		
		{
			AnimationDuration framedelay = toAnimationDuration(g_platformTime.lastFrameDuration());
			Entity * entity = entities.player();
			
			EERIEDrawAnimQuatUpdate(entity->obj,
			                        entity->animlayer.data(),
			                        entity->angle,
			                        entity->pos,
			                        framedelay,
			                        entity,
			                        true);
		}
		
		if((player.Interface & INTER_COMBATMODE) && entities.player()->animlayer[1].cur_anim)
			ManageCombatModeAnimations();

		if(entities.player()->animlayer[1].cur_anim)
			ManageCombatModeAnimationsEND();
	}

	// The weapon in the IK hand (#75): after this frame's body pose and the combat mode's links
	vr::updateHeldWeapon();

	// Physical melee (#39): swings of the held weapon hit - not with a menu open, an item dragged, the controls blocked
	{
		const char * refusal = openMenuName();
		if(!refusal && BLOCK_PLAYER_CONTROLS) {
			refusal = "controls blocked";
		} else if(!refusal && g_draggedEntity) {
			refusal = "an item is dragged";
		} else if(!refusal && player.lifePool.current <= 0.f) {
			refusal = "dead";
		}
		vr::updateMelee(refusal);
	}

	restoreEngineCamera();

	updateFirstPersonCamera();
	g_sceneFirstPos = g_playerCamera.m_pos;

	{
		vr::perf::Scope perfScript(vr::PerfPhase::Script);
		ARX_SCRIPT_Timer_Check();
	}

	speechControlledCinematic();

	handlePlayerDeath();
	
	UpdateCameras();

	ARX_PLAYER_FrameCheck(g_platformTime.lastFrameDuration());

	updateActiveCamera();

	ARX_GLOBALMODS_Apply();
	
	// Set Listener Position
	{
		std::pair<Vec3f, Vec3f> frontUp = angleToFrontUpVec(g_camera->angle);
		ARX_SOUND_SetListener(g_camera->m_pos, frontUp.first, frontUp.second);
	}
	
	// Check For Hiding/unHiding Player Gore
	if(EXTERNALVIEW || player.lifePool.current <= 0) {
		ARX_INTERACTIVE_Show_Hide_1st(entities.player(), false);
	}
	
	if(!EXTERNALVIEW) {
		ARX_INTERACTIVE_Show_Hide_1st(entities.player(), true);
	}
	
	PrepareIOTreatZone();
	{
		vr::perf::Scope perfPhysics(vr::PerfPhase::Physics);
		ARX_PHYSICS_Apply();
	}

	PrecalcIOLighting(g_camera->m_pos, g_camera->cdepth * 0.6f);

	ARX_SCENE_Update();

	{
		vr::perf::Scope perfParticles(vr::PerfPhase::Particles);
		g_particleManager.Update(g_gameTime.lastFrameDuration());
	}

	ARX_FOGS_Render();

	TreatBackgroundActions();

	// Checks Magic Flares Drawing
	if(!player.m_paralysed) {
		if(eeMousePressed1()) {
			if(!ARX_FLARES_Block) {
				static PlatformDuration runeDrawPointElapsed = 0;
				if(!config.input.useAltRuneRecognition) {
					runeDrawPointElapsed += g_platformTime.lastFrameDuration();
					
					if(runeDrawPointElapsed >= runeDrawPointInterval) {
						ARX_SPELLS_AddPoint(DANAEMouse);
						while(runeDrawPointElapsed >= runeDrawPointInterval) {
							runeDrawPointElapsed -= runeDrawPointInterval;
						}
					}
				} else {
					ARX_SPELLS_AddPoint(DANAEMouse);
				}
			} else {
				spellRecognitionPointsReset();
				ARX_FLARES_Block = false;
			}
		} else if(!ARX_FLARES_Block) {
			ARX_FLARES_Block = true;
		}
	}

	ARX_SPELLS_Precast_Check();
	
	if(ARXmenu.mode() == Mode_InGame) {
		ARX_SPELLS_ManageMagic();
	}
	
	ARX_SPELLS_UpdateSymbolDraw();

	ManageTorch();
	
	{
		
		// cfg.ini's FOV is the flat camera's; in a stereo frame the eyes get their own projection from
		// the headset's angles and the head camera the union of both (#19, prepareStereoHeadCamera)
		g_playerCamera.setFov(glm::radians(config.video.fov));

		// FOV zoom: Magic Sight widens the view a little, the bow aim narrows it - changes of the focal
		// length. On a monitor a cue, in a headset a change of the eyes' field of view: the VR comfort
		// policy keeps the focal at the configured FOV (#76; the per-eye projection itself is #19)
		float focalZoom = 0.f;
		
		Spell * spell = spells.getSpellByCaster(EntityHandle_Player, SPELL_MAGIC_SIGHT);
		if(spell) {
			GameDuration duration = g_gameTime.now() - spell->m_timcreation;
			focalZoom -= 30.f * glm::clamp(duration / 500ms, 0.f, 1.f);
		}
		
		focalZoom += 177.5f * player.m_bowAimRatio;
		
		static bool s_zoomSuppressedLogged = false;
		if(focalZoom == 0.f) {
			s_zoomSuppressedLogged = false;
		} else if(vr::comfort().fovZoom) {
			g_playerCamera.focal += focalZoom;
		} else if(!s_zoomSuppressedLogged) {
			// Once per zoom episode (the spell cast, the bow drawn), not per frame
			s_zoomSuppressedLogged = true;
			LogInfo << "[vr] comfort: FOV zoom suppressed - focal " << (focalZoom > 0.f ? "+" : "") << focalZoom
			        << (spell ? " (Magic Sight)" : "") << (player.m_bowAimRatio != 0.f ? " (bow aim)" : "");
		}
		
	}

	// Effects that used to advance while the level was drawn - once per frame here, so the scene
	// can be drawn once per eye without running the game twice (#71)
	{
		vr::perf::Scope perfParticles(vr::PerfPhase::Particles);
		ARX_PARTICLES_Update();
		ParticleSparkUpdate();
		ARX_MAGICAL_FLARES_Update();
		PolyBoomUpdate();
	}
	ARX_DAMAGE_Update_Hit_Blood();
	eyeball.updateFading();
	ARX_SPELLS_Update();
	updateLightFlares();
	ARX_PLAYER_Manage_Death();

	ARX_INTERACTIVE_DestroyIOdelayedExecute();
}

void ArxGame::renderLevel() {

	ARX_PROFILE_FUNC();

	// updateLevel() advanced the game; from here on the frame is only drawn. The scene view may be
	// drawn more than once per frame (once per eye in VR, twice for the test channel's drawcheck):
	// every view must see the same state, and the same random numbers for the same flicker.
	size_t views = channel::sceneViewCount();
	#ifdef ARX_DEBUG
	if(g_debugToggles[9]) {
		views = std::max(views, size_t(2));
	}
	#endif

	// The wrist bars (#116): shown this frame when the player's left arm was posed from a controller
	vr::updateWristVitals();

	if(vr::stereoActive()) {
		// In VR the views are the two eyes, each into its own swapchain image (#19)
		renderStereoViews();
	} else {
		std::optional<Random::State> random;
		if(views > 1) {
			random = Random::getState();
		}
		for(size_t view = 0; view < views; view++) {
			if(view > 0) {
				Random::setState(*random);
			}
			renderSceneView();
			channel::afterSceneView(view);
		}
	}

	renderInterface();

}

/*!
 * The scene once per eye (#19): each eye's image becomes the render target, the eye's camera is the
 * head camera moved to the eye and turned by its cant, with the eye's own off-axis projection from
 * the headset's field of view (research rows F-115, F-162). Both eyes draw the same frame of the
 * game (#71) with the same random numbers. The eye image is the screen for everything drawn into
 * it: full-screen effects, sprite sizes and their culling measure themselves against g_size, which
 * is the eye's size during the draw. Afterwards the head camera is prepared again for the
 * interface, at its own size on the quad (#73), which is neither the eye's nor the window's, and
 * the entities' screen data for mouse picking is the head camera's again (#72).
 */
void ArxGame::renderStereoViews() {

	ARX_PROFILE_FUNC();

	static std::array<Camera, 2> s_eyeCameras;

	Camera * cam = g_camera;
	arx_assert(cam);

	const std::array<vr::EyeRig, 2> & rigs = vr::eyeRigs();
	const Vec2i eyeSize = vr::eyeRenderSize();
	const Rect eyeRect(eyeSize.x, eyeSize.y);
	const glm::mat4x4 headWorldToView = cameraWorldToView(*cam);
	const float nearDist = vr::NearClipMeters * vr::EyeRigParams().unitsPerMeter;

	const Rect interfaceSize = g_size;
	const Vec2f interfaceRatio = g_sizeRatio;
	const Random::State random = Random::getState();

	for(size_t eye = 0; eye < rigs.size(); eye++) {

		if(!vr::beginEyeRender(eye)) {
			// The eye image could not be acquired (logged by the VR module): this frame shows the
			// background in both eyes instead of one live and one stale eye (F-116)
			continue;
		}

		if(eye > 0) {
			Random::setState(random);
		}

		g_size = eyeRect;
		g_sizeRatio = Vec2f(float(eyeRect.width()) / 640.f, float(eyeRect.height()) / 480.f);

		Camera & eyeCamera = s_eyeCameras[eye];
		eyeCamera = *cam;
		eyeCamera.m_pos = vr::eyeWorldPosition(headWorldToView, rigs[eye]);
		eyeCamera.setFov(vr::verticalFov(rigs[eye].fov));
		PrepareCamera(&eyeCamera, eyeRect, vr::eyeWorldToView(headWorldToView, rigs[eye]),
		              vr::eyeProjection(rigs[eye].fov, nearDist, cam->cdepth));

		// Animated entities were projected with the head camera in the update: again for this eye
		g_reprojectEntitiesPerView = true;
		renderSceneView();
		g_reprojectEntitiesPerView = false;

		g_size = interfaceSize;
		g_sizeRatio = interfaceRatio;

		vr::endEyeRender(eye);
	}

	// The cinematic border's scissor belongs to the eye images, not to the interface on the quad
	GRenderer->SetScissor(Rect());

	prepareStereoHeadCamera(cam);

	// The eyes left their screen positions in the entities: picking reads the head camera's (#72)
	ProjectEntitiesForPicking();

}

void ArxGame::renderSceneView() {

	ARX_PROFILE_FUNC();

	// Everything batched from here on belongs to this view
	g_renderBatcher.clear();

	// The previous view may have left the cinematic border's scissor
	GRenderer->SetScissor(Rect());

	// Clear screen & Z buffers
	GRenderer->Clear(Renderer::ColorBuffer | Renderer::DepthBuffer, g_fogColor);

	// The cinematic bars (#29): in VR not into the eyes, where they are black bars glued to the face - the
	// cinemascope state stays for the game's logic. An eye that gets them is counted by the scene meter
	const bool eye = vr::stereoActive();
	if(!eye || vr::letterboxInEyes(vr::sceneState().director.shown())) {
		if(cinematicBorder.render() && eye) {
			vr::sceneState().meter.letterboxDrawn();
		}
	}

	GRenderer->SetAntialiasing(true);
	
	GRenderer->SetFogParams(fZFogStart * g_camera->cdepth, fZFogEnd * g_camera->cdepth);
	GRenderer->SetFogColor(g_fogColor);
	
	ARX_SCENE_Render();
	
	drawDebugRender();

	// Begin Particles
	g_particleManager.Render();

	// In VR the screen-space particles go to the flat frame with the interface (#72)
	ARX_PARTICLES_Render(vr::stereoActive() ? ParticleLayer::World : ParticleLayer::All);
	ParticleSparkRender();
	
	// End Particles

	// Renders Magical Flares
	if(!((player.Interface & INTER_PLAYERBOOK) && !(player.Interface & INTER_COMBATMODE))) {
		ARX_MAGICAL_FLARES_Render();
	}

	// Checks some specific spell FX
	CheckMr();

	if(player.m_improve) {
		DrawImproveVisionInterface();
	}
	
	eyeball.drawMagicSightInterface();

	if(player.m_paralysed) {
		UseRenderState state(render2D().blendAdditive());
		EERIEDrawBitmap(Rectf(g_size), 0.0001f, nullptr, Color::rgb(0.28f, 0.28f, 1.f));
	}

	// Red screen fade for damages.
	ARX_DAMAGE_Show_Hit_Blood();

	// Draw spells
	ARX_SPELLS_Render();

	GRenderer->SetFogColor(Color());
	g_renderBatcher.render();
	GRenderer->SetFogColor(g_fogColor);

	// Health and mana on the left wrist (#116), after everything opaque: the arm and the walls cover them
	vr::drawWristVitals();

	GRenderer->SetAntialiasing(false);

	renderLightFlares();

	// Death visual
	ARX_PLAYER_Render_Death();

	// A scene's subtitles in the world (#29.3)
	if(eye) {
		vr::drawSubtitlesInEye();
	}

	// A head in a wall or too far from the body sees black instead of through the wall (#26, D-018): over
	// the whole view of this eye, under the interface, which stays readable on its own panel. A scene's cut
	// fades the same way (#29): the darker of the two
	const float headFade = std::max(vr::fadeStatus().fade, vr::isEnabled() ? vr::sceneState().director.fade() : 0.f);
	if(headFade > 0.f) {
		UseRenderState state(render2D());
		EERIEDrawBitmap(Rectf(g_size), 0.0001f, nullptr, Color(Color4f(Color3f::black, headFade)));
	}

}

void ArxGame::renderInterface() {

	ARX_PROFILE_FUNC();

	g_renderBatcher.clear();

	if(vr::stereoActive()) {
		// The screen-space particles (the book icon's FX, the book's fire) are window coordinates:
		// under the interface on the flat frame, where the scene pass drew them in flat mode (#72)
		ARX_PARTICLES_Render(ParticleLayer::Screen);
		GRenderer->SetFogColor(Color());
		g_renderBatcher.render();
		GRenderer->SetFogColor(g_fogColor);
		g_renderBatcher.clear();
	}

	// Draw game interface if needed
	if(ARXmenu.mode() == Mode_InGame && !cinematicBorder.isActive()) {
	
		UseTextureState textureState(TextureStage::FilterLinear, TextureStage::WrapClamp);
		
		ARX_INTERFACE_NoteManage();
		g_hudRoot.draw();
		
		if((player.Interface & INTER_PLAYERBOOK) && !(player.Interface & INTER_COMBATMODE)) {
			ARX_MAGICAL_FLARES_Render();
			g_renderBatcher.render();
		}
		
	}

	GRenderer->Clear(Renderer::DepthBuffer);

	// Speech Management
	notification_check();

	if(pTextManage && !pTextManage->Empty()) {
		pTextManage->Update(g_platformTime.lastFrameDuration());
		pTextManage->Render();
	}
	
	if(SHOW_INGAME_MINIMAP &&
	   cinematicIsStopped() &&
	   !cinematicBorder.isActive() &&
	   !BLOCK_PLAYER_CONTROLS &&
	   !(player.Interface & INTER_PLAYERBOOK)) {
		g_miniMap.showPlayerMiniMap(getMapLevelForArea(g_currentArea));
	}
	
	ARX_INTERFACE_RenderCursor(false);
	
	CheatDrawText();

	if(FADEDIR)
		ManageFade();
	
	GRenderer->SetScissor(Rect());

	ARX_SPEECH_Update();

	// A scene's subtitles on the head-locked interface layer (#29.3)
	if(vr::stereoActive()) {
		vr::drawSubtitlesOnInterface();
	}

}

void ArxGame::render() {
	
	ARX_PROFILE_FUNC();
	
	SetActiveCamera(&g_playerCamera);
	
	// Update Various Player Infos for this frame.
	ARX_PLAYER_Frame_Update();
	
	PULSATE = timeWaveSin(g_gameTime.now(), 1600ms * glm::pi<float>());
	EERIEDrawnPolys = 0;
	
	// Checks for Keyboard & Moulinex
	{
		g_cursorOverBook = false;
		
		if(ARXmenu.mode() == Mode_InGame) { // Playing Game
			// Checks Clicks in Book Interface
			if(ARX_INTERFACE_MouseInBook()) {
				g_cursorOverBook = true;
			}
		}
		
		if(vr::reticlesOn()) {
			// VR (#37): in the exploration mode as in the cursor mode, the hand that owns the mouse hovers
			if(!BLOCK_PLAYER_CONTROLS && !g_cursorOverBook && eMouseState != MOUSE_IN_NOTE) {
				FlyingOverIO = FlyingOverObject(DANAEMouse);
			} else {
				FlyingOverIO = nullptr;
			}
		} else if((player.Interface & INTER_COMBATMODE) || PLAYER_MOUSELOOK_ON) {
			FlyingOverIO = nullptr; // Avoid to check with those modes
		} else {
			if(!BLOCK_PLAYER_CONTROLS
				&& !TRUE_PLAYER_MOUSELOOK_ON
				&& !g_cursorOverBook
				&& eMouseState != MOUSE_IN_NOTE
			) {
				FlyingOverIO = FlyingOverObject(DANAEMouse);
			} else {
				FlyingOverIO = nullptr;
			}
		}
		
		if(!player.m_paralysed || ARXmenu.mode() != Mode_InGame) {
			manageKeyMouse();
		}
	}
	
	if(CheckInPoly(player.pos)) {
		LastValidPlayerPos = player.pos;
	}
	
	// Updates Externalview
	EXTERNALVIEW = false;
	
	if(ARXmenu.mode() != Mode_MainMenu) {
		Menu2_Close();
	}
	
	if(ARXmenu.mode() != Mode_InGame) {
		benchmark::begin(benchmark::Menu);
		vr::perf::Scope perfRender(vr::PerfPhase::Render);
		ARX_Menu_Render();
	} else if(isInCinematic()) {
		benchmark::begin(benchmark::Cinematic);
		vr::perf::Scope perfRender(vr::PerfPhase::Render);
		cinematicRender();
	} else {
		benchmark::begin(cinematicBorder.CINEMA_DECAL != 0.f ? benchmark::Cutscene : benchmark::Scene);
		updateLevel();
		vr::perf::Scope perfRender(vr::PerfPhase::Render);
		renderLevel();
	}
	
	if(g_debugInfo != InfoPanelNone) {
		switch(g_debugInfo) {
		case InfoPanelFramerate: {
			g_fpsCounter.CalcFPS();
			ShowFPS();
			break;
		}
		case InfoPanelFramerateGraph: {
			ShowFrameDurationPlot();
			break;
		}
		case InfoPanelDebug: {
			ShowInfoText();
			break;
		}
		case InfoPanelAudio: {
			debugHud_Audio();
			break;
		}
		case InfoPanelCulling: {
			debugHud_Culling();
			break;
		}
		default: break;
		}
	}
	
#ifdef ARX_DEBUG
	ShowDebugToggles();
#endif
	
	g_console.draw();
	
	if(ARXmenu.mode() == Mode_InGame) {
		{
			vr::perf::Scope perfScript(vr::PerfPhase::Script);
			ARX_SCRIPT_AllowInterScriptExec();
			ARX_SCRIPT_EventStackExecute();
		}
		// Updates Damages Spheres
		ARX_DAMAGES_UpdateAll();
		ARX_MISSILES_Update();

		ARX_PATH_UpdateAllZoneInOutInside();
	}

	LastMouseClick = EERIEMouseButton;
	
	gldebug::endFrame();
}

void ArxGame::onRendererInit(Renderer & renderer) {
	
	arx_assert(GRenderer == nullptr);
	
	GRenderer = &renderer;
	
	arx_assert_msg(renderer.getTextureStageCount() >= 3, "not enough texture units");
	arx_assert(m_MainWindow);
	
	renderer.Clear(Renderer::ColorBuffer);
	m_MainWindow->showFrame();
	
	// Restore All Textures RenderState
	renderer.RestoreAllTextures();

	ARX_PLAYER_Restore_Skin();
	
	// Fog
	float fogEnd = 0.48f;
	float fogStart = fogEnd * 0.65f;
	renderer.SetFogParams(fogStart, fogEnd);
	renderer.SetFogColor(g_fogColor);
	
	ComputePortalVertexBuffer();
	std::unique_ptr<VertexBuffer<SMY_VERTEX3>> vb3 = renderer.createVertexBuffer3(4000, Renderer::Stream);
	pDynamicVertexBuffer = new CircularVertexBuffer<SMY_VERTEX3>(std::move(vb3));
	
	size_t size = (config.video.bufferSize < 1) ? 32 * 1024 : config.video.bufferSize * 1024;
	std::unique_ptr<VertexBuffer<TexturedVertex>> vb = renderer.createVertexBufferTL(size, Renderer::Stream);
	pDynamicVertexBuffer_TLVERTEX = new CircularVertexBuffer<TexturedVertex>(std::move(vb));
	
	MenuReInitAll();
	
	// The app is ready to go
	m_bReady = true;
}

void ArxGame::onRendererShutdown(Renderer & renderer) {
	
	if(GRenderer != &renderer) {
		// onRendererInit() failed
		return;
	}
	
	m_bReady = false;
	
	GRenderer->ReleaseAllTextures();

	delete pDynamicVertexBuffer_TLVERTEX, pDynamicVertexBuffer_TLVERTEX = nullptr;
	delete pDynamicVertexBuffer, pDynamicVertexBuffer = nullptr;
	
	EERIE_PORTAL_ReleaseOnlyVertexBuffer();
	
	GRenderer = nullptr;
}
