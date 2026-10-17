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

#include <algorithm>
#include <limits>
#include <string>
#include <vector>

#include "core/Core.h"
#include "core/GameEvent.h"
#include "game/Camera.h"
#include "graphics/Color.h"
#include "graphics/Draw.h"
#include "graphics/Renderer.h"
#include "graphics/data/Mesh.h"
#include "graphics/font/Font.h"
#include "gui/Text.h"
#include "vr/HeadTracking.h"
#include "vr/SceneView.h"
#include "vr/VRSystem.h"

namespace vr {

namespace {

//! The box behind the text: dark, a little see-through, so the line reads against any wall
const Color4f SubtitleBackColor = Color4f::rgba(0.f, 0.f, 0.f, 0.6f);
//! The speaker's name over the line, warm like the game's parchment, the line itself white
const Color SubtitleNameColor = Color::rgb(0.95f, 0.8f, 0.5f);
const Color SubtitleTextColor = Color::white;

struct SubtitleDrawState {

	SubtitleStatus status;

	u64 nextLine = 0;    //!< the number the next line gets
	u64 line = 0;        //!< the line shown now, 0 for none
	u64 countedLine = 0; //!< the newest line counted in \c lines (drawn at least once)
	bool named = false;  //!< status.speaker is a localised name and drawn

	bool hasHead = false;
	Vec3f headTop = Vec3f(0.f);

	//! The placement drawn now: the setting, or \c chest for an \c above without a usable speaker
	SubtitlePlacement drawn = DefaultSubtitlePlacement;
	bool decided = false;  //!< \ref drawn was decided for this line (by the interface pass)

	bool chestPlaced = false;
	Vec3f chestRoom = Vec3f(0.f);   //!< the chest panel in the room (LOCAL, engine axes, meters)
	Vec3f chestWorld = Vec3f(0.f);  //!< the same in the world this frame

};

SubtitleDrawState g_subtitles;

//! The proposals at the world scale of the eyes
SubtitleParams subtitleParams() {
	SubtitleParams params;
	params.unitsPerMeter = EyeRigParams().unitsPerMeter;
	return params;
}

//! The line is drawn: the first time it is counted in \c lines and logged as \c vr.scene.subtitle
void countSubtitleShown() {
	if(g_subtitles.line > g_subtitles.countedLine) {
		g_subtitles.countedLine = g_subtitles.line;
		g_subtitles.status.lines++;
		gameevent::Event("vr.scene.subtitle", 200)("placement", subtitlePlacementName(g_subtitles.status.placement))
			("speaker", g_subtitles.status.speaker)("drawn", subtitlePlacementName(g_subtitles.drawn));
	}
}

//! The head in the room (LOCAL in engine axes, meters) and its angles; the origin for an untracked head
void subtitleHeadInRoom(Vec3f & position, Anglef & headAngles) {
	const HeadStatus & head = headStatus();
	position = toEngineAxes(head.position);
	headAngles = head.head;
}

//! \a text in lines no wider than \a maxWidth, broken between words (a word wider on its own keeps its line)
std::vector<std::string> wrapSubtitle(Font & font, std::string_view text, float maxWidth) {

	std::vector<std::string> lines(1);
	size_t start = 0;
	while(start <= text.size()) {
		size_t end = text.find_first_of(" \n", start);
		if(end == std::string_view::npos) {
			end = text.size();
		}
		const std::string_view word = text.substr(start, end - start);
		if(!word.empty()) {
			std::string & line = lines.back();
			const std::string candidate = line.empty() ? std::string(word) : line + ' ' + std::string(word);
			if(line.empty() || float(font.getTextSize(candidate).width()) <= maxWidth) {
				line = candidate;
			} else {
				lines.emplace_back(word);
			}
		}
		if(end < text.size() && text[end] == '\n' && !lines.back().empty()) {
			lines.emplace_back();
		}
		start = end + 1;
	}
	if(lines.size() > 1 && lines.back().empty()) {
		lines.pop_back();
	}

	return lines;
}

//! The box of the line on its anchor: the speaker's name over the text, wrapped to \a maxWidth
void drawSubtitleBox(const Vec2f & anchor, SubtitleAlign align, float maxWidth, bool lowerThird) {

	Font * font = hFontInGame;
	if(!font || g_subtitles.status.text.empty()) {
		return;
	}

	const std::vector<std::string> lines = wrapSubtitle(*font, g_subtitles.status.text, maxWidth);
	const std::string & name = g_subtitles.status.speaker;
	const float lineHeight = float(font->getLineHeight());
	const float pad = std::max(4.f, lineHeight * 0.4f);

	float width = g_subtitles.named ? float(font->getTextSize(name).width()) : 0.f;
	for(const std::string & line : lines) {
		width = std::max(width, float(font->getTextSize(line).width()));
	}
	const size_t rows = lines.size() + (g_subtitles.named ? 1 : 0);

	const Vec2f size(width + 2.f * pad, float(rows) * lineHeight + 2.f * pad);
	const Rectf box = lowerThird ? subtitleLowerThirdBox(Vec2f(g_size.size()), size)
	                             : subtitleBox(anchor, size, align);

	{
		UseRenderState state(render2D());
		EERIEDrawBitmap(box, 0.f, nullptr, Color(SubtitleBackColor));
	}

	Vec2f pen(box.left + pad, box.top + pad);
	if(g_subtitles.named) {
		font->draw(int(pen.x), int(pen.y), name, SubtitleNameColor);
		pen.y += lineHeight;
	}
	for(const std::string & line : lines) {
		font->draw(int(pen.x), int(pen.y), line, SubtitleTextColor);
		pen.y += lineHeight;
	}

	countSubtitleShown();
}

} // anonymous namespace

const SubtitleStatus & subtitleStatus() {
	return g_subtitles.status;
}

void setSubtitlePlacement(SubtitlePlacement placement) {
	if(g_subtitles.status.placement != placement) {
		g_subtitles.status.placement = placement;
		g_subtitles.decided = false;
	}
}

u64 startSubtitle() {
	g_subtitles.status.voicedLines++;
	return ++g_subtitles.nextLine;
}

void showSubtitle(u64 line, std::string_view speaker, std::string_view text, bool named, const Vec3f * headTop) {

	SubtitleDrawState & s = g_subtitles;
	if(s.line != line) {
		// A new line: its own chest panel where the player looks now, its own decision for above
		s.line = line;
		s.chestPlaced = false;
		s.decided = false;
	}

	// Shown once the eyes see the scene: a scene starts behind a short fade (vr/SceneView.h)
	s.status.visible = (sceneState().director.shown() != SceneShown::None);
	if(s.status.speaker != speaker) {
		s.status.speaker = std::string(speaker);
	}
	if(s.status.text != text) {
		s.status.text = std::string(text);
	}
	s.named = named;
	s.hasHead = (headTop != nullptr);
	if(headTop) {
		s.headTop = *headTop;
	}
}

void hideSubtitle() {
	SubtitleDrawState & s = g_subtitles;
	s.line = 0;
	s.decided = false;
	s.status.visible = false;
	s.status.text.clear();
	s.status.speaker.clear();
}

void noteFlatSubtitle(bool visible, std::string_view speaker, std::string_view text) {
	SubtitleStatus & status = g_subtitles.status;
	status.visible = visible;
	if(status.speaker != speaker) {
		status.speaker = std::string(speaker);
	}
	if(status.text != text) {
		status.text = std::string(text);
	}
}

void resetSubtitleCounts() {
	g_subtitles.status.lines = 0;
	g_subtitles.status.voicedLines = 0;
	// A line already counted before the reset is not counted again in the new stretch
	g_subtitles.countedLine = std::max(g_subtitles.countedLine, g_subtitles.line);
}

void drawSubtitlesInEye() {

	const SubtitleDrawState & s = g_subtitles;
	if(!s.line || !s.status.visible || !s.decided || s.drawn == SubtitlePlacement::LowerThird) {
		return;
	}

	const bool above = (s.drawn == SubtitlePlacement::Above);
	const Vec3f anchor = above ? subtitleAboveAnchor(s.headTop, subtitleParams()) : s.chestWorld;

	// The eye's own projection of a point in the world: the two eyes see the box at the anchor's depth
	const Vec4f p = worldToClipSpace(anchor);
	if(p.w <= 0.f) {
		return;
	}

	drawSubtitleBox(Vec2f(p) / p.w, above ? SubtitleAlign::Above : SubtitleAlign::Center,
	        subtitleMaxWidth(g_preparedCamera.m_pixelsPerUnit, subtitleParams()), false);
}

void drawSubtitlesOnInterface() {

	SubtitleDrawState & s = g_subtitles;
	if(!s.line || !s.status.visible) {
		return;
	}

	// The head camera of this frame (prepared for the interface after the eyes): the eyes of the next
	// frame draw with what is decided here

	// The chest panel stays where it was placed in the room; the room moves with the camera the head
	// is on (a scene's camera, a scripted walk)
	Vec3f head;
	Anglef headAngles;
	subtitleHeadInRoom(head, headAngles);
	if(!s.chestPlaced) {
		s.chestPlaced = true;
		s.chestRoom = subtitleChestPanel(head, headAngles.getYaw(), subtitleParams());
	}
	const Vec3f chestView = subtitleRoomToView(s.chestRoom, head, headAngles, subtitleParams());
	s.chestWorld = Vec3f(g_preparedCamera.m_viewToWorld * Vec4f(chestView, 1.f));

	SubtitlePlacement drawn = s.status.placement;
	if(drawn == SubtitlePlacement::Above) {
		const bool wasAbove = s.decided && s.drawn == SubtitlePlacement::Above;
		const Vec3f view(g_preparedCamera.m_worldToView * Vec4f(subtitleAboveAnchor(s.headTop, subtitleParams()), 1.f));
		if(!s.hasHead || !subtitleAnchorInView(view, tangents(unionEyeFov()), wasAbove, subtitleParams())) {
			drawn = SubtitlePlacement::Chest;
		}
	}
	s.drawn = drawn;
	s.decided = true;

	if(drawn == SubtitlePlacement::LowerThird) {
		drawSubtitleBox(Vec2f(0.f), SubtitleAlign::Above, float(g_size.width()) * subtitleParams().lowerThirdWidth, true);
	}
}

} // namespace vr
