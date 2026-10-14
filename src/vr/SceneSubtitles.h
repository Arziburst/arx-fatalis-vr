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

#ifndef ARX_VR_SCENESUBTITLES_H
#define ARX_VR_SCENESUBTITLES_H

#include <cmath>
#include <optional>
#include <string>
#include <string_view>

#include <glm/glm.hpp>

#include "graphics/Math.h"
#include "math/Angle.h"
#include "math/Types.h"
#include "platform/Platform.h"
#include "vr/EyeProjection.h"
#include "vr/WorldScale.h"

/*!
 * \file
 * \brief The subtitles of scripted scenes in VR (#29.3)
 *
 * In a headset the game's subtitle under the letterbox is a line glued to the face, and a line spoken
 * without the letterbox has none at all. In VR every voiced line of a scene (a \c speak \c -c line, a
 * camera entity, or the letterbox on) gets a subtitle with the speaker's name, wherever a switch puts it
 * (cfg.ini \c vr_subtitles, channel \c scene \c subtitles): above the speaker (the default), on a panel at
 * chest height, or a lower third that follows the head. Flat mode keeps the original band.
 *
 * gui/Speech.cpp says which line plays (\ref startSubtitle, \ref showSubtitle); the eyes' pass draws
 * \c above and \c chest into each eye (\ref drawSubtitlesInEye), the interface layer the lower third
 * (\ref drawSubtitlesOnInterface). The placement math below is pure and host-tested
 * (tests/vr/SceneSubtitlesTest.cpp).
 */
namespace vr {

enum class SubtitlePlacement : u8 {
	Above,     //!< above the speaker's head, in the world
	Chest,     //!< a panel at chest height
	LowerThird //!< a lower third that follows the head
};

constexpr SubtitlePlacement DefaultSubtitlePlacement = SubtitlePlacement::Above;

//! "above", "chest" or "lowerthird" - for cfg.ini, the channel, status.json and the log
inline const char * subtitlePlacementName(SubtitlePlacement placement) {
	switch(placement) {
		case SubtitlePlacement::Above: return "above";
		case SubtitlePlacement::Chest: return "chest";
		case SubtitlePlacement::LowerThird: return "lowerthird";
	}
	return "above";
}

//! \return the placement named by \ref subtitlePlacementName, nothing for an unknown name
inline std::optional<SubtitlePlacement> parseSubtitlePlacement(std::string_view name) {
	for(SubtitlePlacement placement : { SubtitlePlacement::Above, SubtitlePlacement::Chest,
	                                    SubtitlePlacement::LowerThird }) {
		if(name == subtitlePlacementName(placement)) {
			return placement;
		}
	}
	return std::nullopt;
}

//! Where the subtitles go and how wide they get - proposals for the headset, the owner tunes them
struct SubtitleParams {
	float unitsPerMeter = UnitsPerMeter;
	float aboveHeadM = 0.25f;       //!< \c above: the box's lower edge over the top of the speaker's head
	float nearestSpeakerM = 0.6f;   //!< \c above: a speaker nearer to the eyes (the hero himself) gets the chest panel
	float edgeMargin = 0.85f;       //!< \c above: back from the chest panel only this far inside the view's edges
	float chestDistanceM = 0.8f;    //!< \c chest: the panel ahead of the head, horizontally
	float chestDropM = 0.45f;       //!< \c chest: the panel below the eyes
	float widthDeg = 32.f;          //!< the widest box in the world, as the eyes see it
	float lowerThirdWidth = 0.6f;   //!< \c lowerthird: the widest box, of the interface's width
	float lowerThirdBottom = 0.92f; //!< \c lowerthird: the box's lower edge, of the interface's height from the top
};

//! \return the anchor of \c above for a speaker whose head's top is \a headTop (world, y down): the box's lower middle
inline Vec3f subtitleAboveAnchor(const Vec3f & headTop, const SubtitleParams & params = SubtitleParams()) {
	return headTop - Vec3f(0.f, params.aboveHeadM * params.unitsPerMeter, 0.f);
}

/*!
 * \return true when a speaker's anchor can carry the subtitle \c above: far enough ahead and inside the
 *         view, else the subtitle falls back to the chest panel (an off-screen or off-voice speaker, the
 *         hero speaking in first person)
 * \param view    the anchor in the head camera's view space (engine axes: x right, y down, z ahead), game units
 * \param fov     the view's extent at distance 1
 * \param wasShown the subtitle was above this speaker in the last frame: it stays up to the view's edge,
 *                 coming back it must be \ref SubtitleParams::edgeMargin inside (no flicker at the edge)
 */
inline bool subtitleAnchorInView(const Vec3f & view, const FovTangents & fov, bool wasShown,
                                 const SubtitleParams & params = SubtitleParams()) {
	if(!(view.z > params.nearestSpeakerM * params.unitsPerMeter)) {
		return false;
	}
	const float k = wasShown ? 1.f : params.edgeMargin;
	const float x = view.x / view.z;
	const float up = -view.y / view.z;
	return x >= fov.left * k && x <= fov.right * k && up >= fov.down * k && up <= fov.up * k;
}

/*!
 * \return the chest panel's middle in the room (LOCAL in engine axes: y down, z ahead at yaw 0; meters)
 *         for a head at \a head turned \a yawDeg: ahead along the yaw, below the eyes
 */
inline Vec3f subtitleChestPanel(const Vec3f & head, float yawDeg, const SubtitleParams & params = SubtitleParams()) {
	const glm::mat4 toView = toRotationMatrix(Anglef(0.f, yawDeg, 0.f));
	const Vec3f forward(glm::transpose(toView) * Vec4f(0.f, 0.f, 1.f, 0.f));
	return head + forward * params.chestDistanceM + Vec3f(0.f, params.chestDropM, 0.f);
}

/*!
 * \return a point of the room (LOCAL in engine axes, meters) in the view space of a head at \a head with
 *         the angles \a angles (both LOCAL), game units - which the head camera turns into the world
 */
inline Vec3f subtitleRoomToView(const Vec3f & point, const Vec3f & head, const Anglef & headAngles,
                                const SubtitleParams & params = SubtitleParams()) {
	return Vec3f(toRotationMatrix(headAngles) * Vec4f(point - head, 0.f)) * params.unitsPerMeter;
}

//! \return the widest box in pixels for a view with \a pixelsPerUnit at distance 1 (PreparedCamera)
inline float subtitleMaxWidth(float pixelsPerUnit, const SubtitleParams & params = SubtitleParams()) {
	return 2.f * std::tan(glm::radians(params.widthDeg) * 0.5f) * pixelsPerUnit;
}

//! How a box sits on its projected anchor
enum class SubtitleAlign {
	Above,  //!< the anchor is the box's lower middle (above the speaker)
	Center  //!< the anchor is the box's middle (the chest panel)
};

//! \return the box of \a size on the projected \a anchor, in pixels
inline Rectf subtitleBox(const Vec2f & anchor, const Vec2f & size, SubtitleAlign align) {
	const float top = (align == SubtitleAlign::Above) ? anchor.y - size.y : anchor.y - size.y * 0.5f;
	return Rectf(Vec2f(anchor.x - size.x * 0.5f, top), size.x, size.y);
}

//! \return the lower third's box of \a size on an interface of \a canvas pixels: centred, low in the view
inline Rectf subtitleLowerThirdBox(const Vec2f & canvas, const Vec2f & size,
                                   const SubtitleParams & params = SubtitleParams()) {
	return subtitleBox(Vec2f(canvas.x * 0.5f, canvas.y * params.lowerThirdBottom), size, SubtitleAlign::Above);
}

//! status.json \c scene.subtitle
struct SubtitleStatus {
	SubtitlePlacement placement = DefaultSubtitlePlacement;
	bool visible = false;   //!< a subtitle is shown now
	std::string text;       //!< the line as shown
	std::string speaker;    //!< the speaker's display name
	u64 lines = 0;          //!< voiced lines shown since the meter reset
	u64 voicedLines = 0;    //!< voiced lines started since the meter reset
};

const SubtitleStatus & subtitleStatus();

//! The placement now (cfg.ini, the channel)
void setSubtitlePlacement(SubtitlePlacement placement);

/*!
 * #29.1's stand-in, called from ArxGame.cpp updateSceneView until the lead removes the call at the
 * integration of #29: does nothing, gui/Speech.cpp notes the lines (\ref startSubtitle)
 */
void noteSubtitle(std::string_view speaker, std::string_view text, bool voiced);

//! #29.1's stand-in, called from ArxGame.cpp updateSceneView until the integration of #29: does nothing
void clearSubtitle();

/*!
 * A voiced line of a scene starts in VR (gui/Speech.cpp): counted in \c voicedLines. Once its subtitle is
 * drawn it counts in \c lines and is logged as \c vr.scene.subtitle \c placement=<p> \c speaker=<name>.
 * \return the line's number for \ref showSubtitle
 */
u64 startSubtitle();

/*!
 * The line the subtitle shows this frame in VR: the newest voiced line of a scene still playing
 * \param named   \a speaker is the speaker's localised name, drawn over the text (else only the status has it)
 * \param headTop the top of the speaker's head in the world, nullptr for none (an off-voice line)
 */
void showSubtitle(u64 line, std::string_view speaker, std::string_view text, bool named, const Vec3f * headTop);

//! No line to show in VR
void hideSubtitle();

//! Flat mode: the original band shows \a text by \a speaker, or nothing (\a visible false) - the status only
void noteFlatSubtitle(bool visible, std::string_view speaker = std::string_view(),
                      std::string_view text = std::string_view());

//! A new measuring stretch (\c scene \c meter \c reset): the line counts start again
void resetSubtitleCounts();

//! \c above and \c chest drawn into the eye being rendered (the scene pass, once per eye)
void drawSubtitlesInEye();

/*!
 * The lower third drawn on the head-locked interface layer; also where the head camera of the frame
 * decides between \c above and its chest fallback and carries the chest panel along with the room
 */
void drawSubtitlesOnInterface();

} // namespace vr

#endif // ARX_VR_SCENESUBTITLES_H
