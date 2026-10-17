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

#include <array>
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
		bool vrHudFrame; //!< VR: the original screen-frame HUD drawn on the 2D interface after all (#469)
		std::string vrHudKeep; //!< VR: HUD elements kept on the 2D interface, comma-separated (#469, vr/HudFrame.h)
		std::string vrLevelUp; //!< VR: the level-up indication - both, radial, wrist or off (#469)
		bool vrPhysicalCrouch; //!< VR: the head going down crouches the character (#100)
		float vrCrouchEnter; //!< VR: the head this far below the calibrated eye crouches, meters (#100)
		float vrCrouchExit; //!< VR: the character stands again once the head is back above this depth, meters (#100)
		std::string vrViewJump; //!< VR: how the view follows a jump: level or arc (#416, vr/StableView.h)
		std::string vrViewCrouch; //!< VR: how fast the view glides into and out of a crouch: slow, medium or fast (#416)
		std::string vrDeathView; //!< VR: what the still ghost sees at death: headless or body (#416)

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
		float vrArmForearmTwist; //!< VR: the forearm's share [0, 1] of the hand's roll off the arm's bend plane (#304)
		float vrArmUpperTwist; //!< VR: the upper arm's share [0, 1] of the forearm's roll (#304)
		float vrArmElbowRoll; //!< VR: the share [0, 1] of the hand's roll that swings the elbow about the arm (#304)
		float vrArmTwistOverflow; //!< VR: degrees past a half turn the hand's roll keeps its side before flipping (#304)
		float vrHeightOffset; //!< VR: the character height setting, meters above the character's eye (#260)
		std::string vrArmsModel; //!< VR: the arms drawn on the player: "classic" (the Arx arms fixed, #466.1) - the only player choice (#466); the test channel may set the others (vr/VRArms.h, #486)
		std::string vrClassicHand; //!< VR: the classic arm's hand, "arx" (the default) or "glove" (ours at the Arx wrist, #466.2)
		float vrHandScale; //!< VR: the custom hands' size against the glove's real size (#385)
		std::string vrBody; //!< VR: the player model drawn under a tracked head, "full" (pushed back) or "hidden" (the arms only, #385.11)
		float vrArmMaxStretch; //!< VR: how far [0, 0.3] the custom arm stretches past its fitted length to reach the controller (#385.6)
		float vrArmWeaponElbowDrop; //!< VR: the share [0, 1] of its angle the elbow of a hand holding a weapon drops by (#385.6)
		float vrMeleeSwingSpeed; //!< VR: m/s a weapon swing hits from (#39, vr/MeleeSwing.h)
		//! VR: per weapon class (vr::MeleeClass) a slash's "<path min> <path max> <speed min> <speed max>", m and m/s;
		//! empty = the class's default (#465, vr::defaultRanges)
		std::array<std::string, 5> vrMeleeSlash;
		std::array<std::string, 5> vrMeleeStab; //!< VR: the same for a stab: its lunge along the blade (#465)
		std::string vrMeleeQuality; //!< VR: how size and speed combine: "min" (the default), "geomean", "product" (#465)
		float vrMeleePathScale; //!< VR: the path ranges scaled by it, e.g. for seated play (#465)
		std::string vrMeleeNoSwing; //!< VR: a hit outside a swing or below a threshold deals "min" (the default) or "none" (#465)
		float vrMeleeTargetCooldown; //!< VR: seconds before a new swing can hit the same target (#39)
		float vrMeleeRecoveryDagger; //!< VR: seconds after a dagger hit before a new swing hits; -1 = the default, 0.6 (#323)
		float vrMeleeRecovery1h; //!< VR: the same for one-handed weapons; the default 0.8 (#323)
		float vrMeleeRecovery2h; //!< VR: the same for two-handed weapons; the default 1.0 (#323)
		// VR: how far a custom hand on a weapon's hilt is closed, degrees (#385.7, vr/HandRig.h HiltAngles)
		float vrHiltKnuckle; //!< the four fingers' knuckles
		float vrHiltMiddle;  //!< their middle joints
		float vrHiltLast;    //!< their last joints
		float vrHiltThumb;   //!< the thumb's two outer joints
		float vrSwordTilt; //!< VR: the sword's forward tilt in the custom hand, degrees: 0, -15 or -30 (#385.10, vr/WeaponGrip.h)
		float vrBodyPush; //!< VR: game units (cm) the own body sits behind the eyes along its facing, looking ahead (#385.9, vr/BodyView.h)
		float vrBodyPushDown; //!< VR: game units (cm) the push grows by looking straight down (#385.9)
		float vrBodyClearance; //!< VR: game units (cm) from an eye within which the own body's faces are not drawn (#385.9)
		//! VR: where a radial menu opens (#377): "hand", or "gaze_when_off" - before the eyes at the hand's height
		//! when the hand is more than \ref vrRadialGazeDeg off the gaze
		std::string vrRadialPlace;
		float vrRadialGazeDeg; //!< VR: degrees off the gaze beyond which gaze_when_off moves the ring (#377)
		float vrGripPress; //!< VR: the Index grip value [0, 1] that presses the grip (#496, vr/VRControls.h IndexGripThreshold)
		float vrGripRelease; //!< VR: the Index grip value it is let go below, under \ref vrGripPress (#496)

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
