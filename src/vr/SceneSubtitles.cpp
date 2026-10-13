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

#include "vr/SceneSubtitles.h"

#include "core/GameEvent.h"

namespace vr {

namespace {

SubtitleStatus g_subtitles;

} // anonymous namespace

const SubtitleStatus & subtitleStatus() {
	return g_subtitles;
}

void setSubtitlePlacement(SubtitlePlacement placement) {
	g_subtitles.placement = placement;
}

void noteSubtitle(std::string_view speaker, std::string_view text, bool voiced) {
	g_subtitles.visible = true;
	g_subtitles.text = std::string(text);
	g_subtitles.speaker = std::string(speaker);
	if(voiced) {
		g_subtitles.voicedLines++;
		g_subtitles.lines++;
	}
	gameevent::Event("vr.scene.subtitle", 200)("placement", subtitlePlacementName(g_subtitles.placement))
		("speaker", g_subtitles.speaker);
}

void clearSubtitle() {
	g_subtitles.visible = false;
	g_subtitles.text.clear();
	g_subtitles.speaker.clear();
}

void resetSubtitleCounts() {
	g_subtitles.lines = 0;
	g_subtitles.voicedLines = 0;
}

void drawSubtitlesInEye() {
	// #29.3
}

void drawSubtitlesOnInterface() {
	// #29.3
}

} // namespace vr
