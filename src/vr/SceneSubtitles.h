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

#include <optional>
#include <string>
#include <string_view>

#include "platform/Platform.h"

/*!
 * \file
 * \brief The subtitles of scripted scenes in VR (#29.3)
 *
 * In a headset the game's subtitle under the letterbox is a line glued to the face. Where it goes instead
 * is a switch (cfg.ini \c vr_subtitles, channel \c scene \c subtitles): above the speaker (the default), on a
 * panel at chest height, or a lower third that follows the head.
 *
 * #29.1 lays the interface: the placement, the status (status.json \c scene.subtitle) and the draw calls
 * from the eyes' pass and the interface layer; #29.3 draws.
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
 * A voiced line starts and its subtitle is shown: fills \ref subtitleStatus and logs
 * \c vr.scene.subtitle \c placement=<p> \c speaker=<name>.
 *
 * Called by #29.1 from the scene view when a \c speak \c -c line starts in VR (ArxGame.cpp,
 * updateSceneView); #29.3 calls it where a subtitle is really shown (gui/Speech.cpp) instead.
 */
void noteSubtitle(std::string_view speaker, std::string_view text, bool voiced);

//! The subtitle shown last is gone
void clearSubtitle();

//! A new measuring stretch (\c scene \c meter \c reset): the line counts start again
void resetSubtitleCounts();

//! The subtitles drawn into the eye being rendered (the scene pass, once per eye): #29.3
void drawSubtitlesInEye();

//! The subtitles drawn on the head-locked interface layer: #29.3
void drawSubtitlesOnInterface();

} // namespace vr

#endif // ARX_VR_SCENESUBTITLES_H
