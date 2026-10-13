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

#ifndef ARX_CORE_CONFIG_H
#define ARX_CORE_CONFIG_H

#include <string>

#include "audio/AudioTypes.h"

#include "input/InputKey.h"

#include "io/fs/FilePath.h"

#include "math/Types.h"
#include "math/Vector.h"

#include "window/DisplayMode.h"

//! Enum for all the controlling actions
enum ControlAction {
	CONTROLS_CUST_JUMP = 0,
	CONTROLS_CUST_MAGICMODE,
	CONTROLS_CUST_STEALTHMODE,
	CONTROLS_CUST_WALKFORWARD,
	CONTROLS_CUST_WALKBACKWARD,
	CONTROLS_CUST_STRAFELEFT,
	CONTROLS_CUST_STRAFERIGHT,
	CONTROLS_CUST_LEANLEFT,
	CONTROLS_CUST_LEANRIGHT,
	CONTROLS_CUST_CROUCH,
	CONTROLS_CUST_USE,
	CONTROLS_CUST_ACTION,
	CONTROLS_CUST_INVENTORY,
	CONTROLS_CUST_BOOK,
	CONTROLS_CUST_BOOKCHARSHEET,
	CONTROLS_CUST_BOOKSPELL,
	CONTROLS_CUST_BOOKMAP,
	CONTROLS_CUST_BOOKQUEST,
	CONTROLS_CUST_DRINKPOTIONLIFE,
	CONTROLS_CUST_DRINKPOTIONMANA,
	CONTROLS_CUST_DRINKPOTIONCURE,
	CONTROLS_CUST_TORCH,
	CONTROLS_CUST_PRECAST1,
	CONTROLS_CUST_PRECAST2,
	CONTROLS_CUST_PRECAST3,
	CONTROLS_CUST_WEAPON,
	CONTROLS_CUST_QUICKLOAD,
	CONTROLS_CUST_QUICKSAVE,
	CONTROLS_CUST_TURNLEFT,
	CONTROLS_CUST_TURNRIGHT,
	CONTROLS_CUST_LOOKUP,
	CONTROLS_CUST_LOOKDOWN,
	CONTROLS_CUST_STRAFE,
	CONTROLS_CUST_CENTERVIEW,
	CONTROLS_CUST_FREELOOK,
	CONTROLS_CUST_PREVIOUS,
	CONTROLS_CUST_NEXT,
	CONTROLS_CUST_CROUCHTOGGLE,
	CONTROLS_CUST_UNEQUIPWEAPON,
	CONTROLS_CUST_CANCELCURSPELL,
	CONTROLS_CUST_MINIMAP,
	CONTROLS_CUST_TOGGLE_FULLSCREEN,
	CONTROLS_CUST_CONSOLE,
	CONTROLS_CUST_DEBUG,
	NUM_ACTION_KEY
};

enum CinematicWidescreenMode {
	CinematicLetterbox = 0,
	CinematicHardEdges = 1,
	CinematicFadeEdges = 2
};

enum UIScaleFilter {
	UIFilterNearest = 0,
	UIFilterBilinear = 1
};

enum QuickLevelTransition {
	NoQuickLevelTransition = 0,
	JumpToChangeLevel = 1,
	ChangeLevelImmediately = 2,
};

enum AutoReadyWeapon {
	NeverAutoReadyWeapon = 0,
	AutoReadyWeaponNearEnemies = 1,
	AlwaysAutoReadyWeapon = 2,
};

struct ActionKey {
	
	explicit constexpr ActionKey(InputKeyId key_0 = UNUSED,
	                             InputKeyId key_1 = UNUSED)
		: key{ key_0, (key_0 != UNUSED && key_0 == key_1) ? UNUSED : key_1 }
	{ }
	
	InputKeyId key[2];
	static const InputKeyId UNUSED = -1;
};

class Config {
	
public:
	
	// section 'video'
	struct {
		
		std::string renderer;
		
		bool fullscreen;
		DisplayMode mode;
		float gamma;
		
		int vsync;
		int fpsLimit;
		int vrFrameDivisor; //!< VR: submit every n-th display slot (the even-cadence cap, #20); 1 = every slot
		bool vrMirror; //!< VR: the desktop window shows what one eye sees (#21); off = the flat frame
		int vrMirrorEye; //!< VR: the eye the desktop mirror shows: 0 = left, 1 = right (default, the owner's choice)
		int vrMsaa; //!< VR: multisampling of the eye images, samples per pixel; 0 = off (#226)
		float vrBodyFollowRadius; //!< VR: meters the head may be from the body before the body steps after it (#26)
		float vrWallFadeOut; //!< VR: seconds to black when the head enters geometry or gets too far from the body (#26)
		float vrWallFadeIn; //!< VR: seconds back from black (#26)
		float vrHeadFarStart; //!< VR: meters from the body where a head too far starts to fade the world (#26)
		float vrHeadFarFull; //!< VR: meters from the body where it is black (#26)
		bool vrRealStepNoise; //!< VR: the body walking after the head makes footstep noise like walking (#26)
		float vrPanelDistance; //!< VR: the menu panel this far ahead of the head, meters (#49)
		float vrPanelDrop; //!< VR: the menu panel's centre this far below the eyes, meters (#49)
		float vrPanelWidth; //!< VR: the menu panel's width, meters (#49)
		bool vrPhysicalCrouch; //!< VR: the head going down crouches the character (#100)
		float vrCrouchEnter; //!< VR: the head this far below the calibrated eye crouches, meters (#100)
		float vrCrouchExit; //!< VR: the character stands again once the head is back above this depth, meters (#100)

		float fov;
		bool viewBobbing;
		bool screenShake;
		
		int levelOfDetail;
		float fogDistance;
		bool antialiasing;
		int maxAnisotropicFiltering;
		bool colorkeyAntialiasing;
		int alphaCutoutAntialiasing;
		
		int bufferSize;
		std::string bufferUpload;
		std::string extensionOverride;
		
	} video;
	
	// section 'interface'
	struct {
		
		std::string language;
		
		bool showCrosshair;
		
		bool limitSpeechWidth;
		CinematicWidescreenMode cinematicWidescreenMode;
		
		float hudScale;
		bool hudScaleInteger;
		float bookScale;
		bool bookScaleInteger;
		float cursorScale;
		bool cursorScaleInteger;
		UIScaleFilter scaleFilter;
		
		float fontSize;
		int fontWeight;
		
		Vec2i thumbnailSize;
		
	} interface;
	
	// section 'window'
	struct {
		
		Vec2i size;
		
		bool minimizeOnFocusLost;
		
	} window;
	
	// section 'audio'
	struct {
		
		std::string language;
		
		std::string backend;
		std::string device;
		
		float volume;
		float sfxVolume;
		float speechVolume;
		float ambianceVolume;
		
		bool eax;
		audio::HRTFAttribute hrtf;
		bool muteOnFocusLost;
		
	} audio;
	
	// section 'input'
	struct {
		
		bool invertMouse;
		AutoReadyWeapon autoReadyWeapon;
		bool mouseLookToggle;
		bool autoDescription;
		int mouseSensitivity;
		int mouseAcceleration;
		bool rawMouseInput;
		bool borderTurning;
		bool useAltRuneRecognition;
		bool improvedBowAim;
		float vrSneakBelow; //!< VR: a running stick walk sneaks below this deflection (#33, D-019)
		float vrRunAbove; //!< VR: a sneaking stick walk runs at this deflection or more (#33, D-019)
		QuickLevelTransition quickLevelTransition;
		bool allowConsole;
		bool vrSmoothTurn; //!< VR: the right stick turns smoothly instead of in snaps (#32)
		float vrSnapTurnAngle; //!< VR: degrees per snap turn (#32)
		float vrSmoothTurnSpeed; //!< VR: smooth turn degrees per second at full deflection (#32)
		float vrSnapTurnRepeat; //!< VR: seconds between snaps while the stick is held; 0 = no repeat (#32)
		float vrHeightOffset; //!< VR: the character height setting, meters above the character's eye (#260)
		float vrMeleeSwingSpeed; //!< VR: m/s a weapon swing hits from (#39, vr/MeleeSwing.h)
		float vrMeleeFullSpeed; //!< VR: m/s a swing has its full damage at (#39)
		float vrMeleeWeakAim; //!< VR: the damage ratio of an uncharged swing; a full charge is 1 (#39)
		float vrMeleeTargetCooldown; //!< VR: seconds before a new swing can hit the same target (#39)
		std::string vrMeleeChargeCue; //!< VR: how the melee charge is shown (#322, vr/MeleeChargeCue.h)

	} input;
	
	// section 'key'
	ActionKey actions[NUM_ACTION_KEY];
	
	enum MigrationStatus {
		OriginalAssets = 0,
		CaseSensitiveFilenames = 1
	};
	
	// section 'misc'
	struct {
		
		bool forceToggle; // should be in input?
		
		MigrationStatus migration;
		
		int quicksaveSlots;
		
		std::string debug; //!< Logger debug levels.
		
		std::string realtimeOverride;
		
	} misc;
	
	bool setActionKey(ControlAction actionId, size_t index, InputKeyId key);
	void setDefaultActionKeys();
	
	/*!
	 * Saves all config entries to a file.
	 * \return true if the config was saved successfully.
	 */
	bool save();
	
	bool init(const fs::path & file);
	
	void setOutputFile(const fs::path & file);
	
private:
	
	fs::path m_file;
	
};

extern Config config;

#endif // ARX_CORE_CONFIG_H
