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
#include <array>
#include <limits>
#include <string>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "core/Core.h"
#include "core/GameEvent.h"
#include "core/GameTime.h"
#include "game/Camera.h"
#include "graphics/Color.h"
#include "graphics/Draw.h"
#include "graphics/Renderer.h"
#include "graphics/data/Mesh.h"
#include "graphics/Vertex.h"
#include "graphics/font/Font.h"
#include "graphics/texture/Texture.h"
#include "gui/Text.h"
#include "vr/HeadPose.h"
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
	bool decided = false;  //!< \ref drawn and the panel were decided for this line (by the interface pass)

	// The panel fixed in the world for the line (the owner, 2026-10-17): placed once, never turning with the head
	Vec3f anchorWorld = Vec3f(0.f); //!< \c above: the box's lower middle; \c chest: its middle (world, game units)
	Vec3f facingWorld = Vec3f(0.f); //!< the player's eye when the line started: the panel faces it
	float metersPerPixel = 0.f;     //!< an eye's pixel at the panel's distance when placed
	float eyePixelsPerUnit = 0.f;   //!< the last eye's pixels per view unit at depth 1
	bool imageReady = false;        //!< the panel's image holds this line at its place: the eyes draw it
	bool panelLogged = false;       //!< the line's placement was logged (vr.subtitle.panel)
	bool gliding = false;           //!< the hero's panel is on its way back below the centre of the view
	float glideT = 0.f;             //!< 0..1 along that way
	Vec3f glideFrom = Vec3f(0.f);   //!< where it started
	std::array<Vec3f, 4> cornersWorld = { }; //!< the panel's corners in the world: top left, top right, bottom right, bottom left
	float eyePixels[2] = { 0.f, 0.f };       //!< the pixels the panel covered in the eyes drawn this frame
	int eyeDraws = 0;                         //!< eyes the panel was drawn into this frame

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
		s.decided = false;
		s.imageReady = false;
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
	s.imageReady = false;
	s.status.panel = false;
	s.status.panelPixels = 0.f;
	s.status.visible = false;
	s.status.text.clear();
	s.status.speaker.clear();
}

void replaceSubtitlePanel() {
	g_subtitles.decided = false;
	g_subtitles.imageReady = false;
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

	SubtitleDrawState & s = g_subtitles;
	// The panel's text has the size it had drawn into the eyes: an eye's pixel at the panel's distance
	s.eyePixelsPerUnit = g_preparedCamera.m_pixelsPerUnit;
	if(!s.line || !s.status.visible || !s.decided || !s.imageReady || s.drawn == SubtitlePlacement::LowerThird) {
		return;
	}
	Texture * texture = subtitleTexture();
	if(!texture) {
		return;
	}

	// The panel's corners through this eye's own projection (x, y, z and w homogeneous, as a sprite's); the image's
	// row 0 is the panel's bottom (drawn through the renderer into a GL texture), so v runs up
	static const Vec2f uv[4] = { Vec2f(0.f, 1.f), Vec2f(1.f, 1.f), Vec2f(1.f, 0.f), Vec2f(0.f, 0.f) };
	const ColorRGBA white = Color::white.toRGBA();
	TexturedVertex v[4];
	Vec2f screen[4];
	for(size_t i = 0; i < 4; i++) {
		worldToClipSpace(s.cornersWorld[i], v[i]);
		if(!(v[i].w > 0.f)) {
			return; // a corner behind the eye
		}
		v[i].color = white;
		v[i].uv = uv[i];
		screen[i] = Vec2f(v[i].p) / v[i].w;
	}

	{
		// Premultiplied over the world, no depth test: always readable (D-055)
		UseRenderState state(render2D().blend(BlendOne, BlendInvSrcAlpha));
		UseTextureState textureState(TextureStage::FilterLinear, TextureStage::WrapClamp);
		GRenderer->SetTexture(0, texture);
		EERIEDRAWPRIM(Renderer::TriangleFan, v, 4);
		GRenderer->ResetTexture(0);
	}

	// The pixels it covers in this eye: its outline clamped to the viewport (status.json scene.subtitle.panelPixels)
	const Rect & viewport = g_preparedCamera.m_viewport;
	float area = 0.f;
	for(size_t i = 0; i < 4; i++) {
		const Vec2f a(glm::clamp(screen[i].x, float(viewport.left), float(viewport.right)),
		              glm::clamp(screen[i].y, float(viewport.top), float(viewport.bottom)));
		const Vec2f b(glm::clamp(screen[(i + 1) % 4].x, float(viewport.left), float(viewport.right)),
		              glm::clamp(screen[(i + 1) % 4].y, float(viewport.top), float(viewport.bottom)));
		area += a.x * b.y - b.x * a.y;
	}
	if(s.eyeDraws < 2) {
		s.eyePixels[s.eyeDraws] = std::abs(area) * 0.5f;
	}
	s.eyeDraws++;
}

//! \return how far \a point is off the head camera's forward, horizontally, in degrees (0 straight ahead)
static float yawFromHeadDeg(const Vec3f & eye, const Vec3f & point) {
	Vec3f forward(g_preparedCamera.m_viewToWorld * Vec4f(0.f, 0.f, 1.f, 0.f));
	Vec3f to = point - eye;
	forward.y = 0.f;
	to.y = 0.f;
	if(glm::length(forward) < 1e-4f || glm::length(to) < 1e-4f) {
		return 0.f;
	}
	return glm::degrees(std::acos(glm::clamp(glm::dot(glm::normalize(forward), glm::normalize(to)), -1.f, 1.f)));
}

//! \return a world point (game units) in the LOCAL space (OpenXR axes, meters) of the head camera prepared now
static Vec3f worldToLocal(const Vec3f & world, const Vec3f & head, const Anglef & headAngles) {
	const Vec3f view(g_preparedCamera.m_worldToView * Vec4f(world, 1.f));
	const glm::mat3 roomToView(toRotationMatrix(headAngles));
	const Vec3f room = head + glm::transpose(roomToView) * (view / subtitleParams().unitsPerMeter);
	return toEngineAxes(room);
}

void drawSubtitlesOnInterface() {

	SubtitleDrawState & s = g_subtitles;
	// What the eyes of this frame drew (they ran before the interface): the status, then the count starts anew
	s.status.panel = (s.eyeDraws >= 2 && s.eyePixels[0] > 0.f && s.eyePixels[1] > 0.f);
	s.status.panelPixels = s.status.panel ? std::min(s.eyePixels[0], s.eyePixels[1]) : 0.f;
	s.eyeDraws = 0;
	s.eyePixels[0] = s.eyePixels[1] = 0.f;
	if(!s.line || !s.status.visible) {
		return;
	}

	// The head camera of this frame (prepared for the interface after the eyes)
	const SubtitleParams params = subtitleParams();
	Vec3f head;
	Anglef headAngles;
	subtitleHeadInRoom(head, headAngles);

	const Vec3f eye(g_preparedCamera.m_viewToWorld * Vec4f(0.f, 0.f, 0.f, 1.f));
	if(!s.decided) {
		// An NPC's line is always over his head, in view or not (the owner, 2026-10-18); the hero's own line and an
		// off-voice one (no head) go below the centre of the view
		SubtitlePlacement drawn = s.status.placement;
		if(drawn == SubtitlePlacement::Above && !s.hasHead) {
			drawn = SubtitlePlacement::Chest;
		}
		if(drawn == SubtitlePlacement::Above) {
			s.anchorWorld = subtitleAboveAnchor(s.headTop, params);
		} else if(drawn == SubtitlePlacement::Chest) {
			const Vec3f chestRoom = subtitleChestPanel(head, headAngles.getYaw(), params);
			const Vec3f chestView = subtitleRoomToView(chestRoom, head, headAngles, params);
			s.anchorWorld = Vec3f(g_preparedCamera.m_viewToWorld * Vec4f(chestView, 1.f));
		}
		const float distanceM = glm::distance(s.anchorWorld, eye) / params.unitsPerMeter;
		const float pixelsPerUnit = s.eyePixelsPerUnit > 0.f ? s.eyePixelsPerUnit : g_preparedCamera.m_pixelsPerUnit;
		s.metersPerPixel = pixelsPerUnit > 0.f ? distanceM / pixelsPerUnit * params.panelScale : 0.f;
		s.panelLogged = false;
		s.gliding = false;
		s.drawn = drawn;
		s.status.drawn = drawn;
		s.decided = true;
	}

	// The lazy follow (the owner, 2026-10-18): the panel stays put in the world - readable and still - and glides to
	// where it belongs, smoothly, once that is far enough: the hero's panel when the head is more than followDeg off it,
	// the NPC's when his head moved aboveFollowM (he walks out while he talks; his idle sway moves nothing)
	Vec3f ideal = s.anchorWorld;
	bool wanted = false;
	if(s.drawn == SubtitlePlacement::Above) {
		ideal = subtitleAboveAnchor(s.headTop, params);
		wanted = glm::distance(ideal, s.anchorWorld) > params.aboveFollowM * params.unitsPerMeter;
	} else if(s.drawn == SubtitlePlacement::Chest) {
		const Vec3f chestRoom = subtitleChestPanel(head, headAngles.getYaw(), params);
		const Vec3f chestView = subtitleRoomToView(chestRoom, head, headAngles, params);
		ideal = Vec3f(g_preparedCamera.m_viewToWorld * Vec4f(chestView, 1.f));
		wanted = yawFromHeadDeg(eye, s.anchorWorld) > params.followDeg;
	}
	if(!s.gliding && wanted) {
		s.gliding = true;
		s.glideT = 0.f;
		s.glideFrom = s.anchorWorld;
	}
	if(s.gliding) {
		const float dt = toMsf(g_platformTime.lastFrameDuration()) / 1000.f;
		s.glideT += params.followSeconds > 0.f ? dt / params.followSeconds : 1.f;
		if(s.glideT >= 1.f) {
			s.anchorWorld = ideal;
			s.gliding = false;
		} else {
			const float t = s.glideT * s.glideT * (3.f - 2.f * s.glideT);
			s.anchorWorld = glm::mix(s.glideFrom, ideal, t);
		}
	}
	s.facingWorld = eye;
	s.status.panelYawFromHeadDeg = yawFromHeadDeg(eye, s.anchorWorld);
	if(s.drawn == SubtitlePlacement::LowerThird) {
		drawSubtitleBox(Vec2f(0.f), SubtitleAlign::Above, float(g_size.width()) * params.lowerThirdWidth, true);
		return;
	}
	if(s.metersPerPixel <= 0.f || !beginSubtitleImage()) {
		return;
	}

	// The box in the middle of its image, its lower edge on the image's for above (the box over the head)
	const Vec2f image{ float(SubtitleImageWidth), float(SubtitleImageHeight) };
	const bool above = (s.drawn == SubtitlePlacement::Above);
	const float pixelsPerUnit = s.eyePixelsPerUnit > 0.f ? s.eyePixelsPerUnit : g_preparedCamera.m_pixelsPerUnit;
	const float maxWidth = std::min(subtitleMaxWidth(pixelsPerUnit, params), image.x * 0.9f);
	{
		GRenderer->SetViewport(Rect(SubtitleImageWidth, SubtitleImageHeight));
		drawSubtitleBox(above ? Vec2f(image.x * 0.5f, image.y) : image * 0.5f,
		                above ? SubtitleAlign::Above : SubtitleAlign::Center, maxWidth, false);
		GRenderer->SetViewport(g_preparedCamera.m_viewport);
	}
	endSubtitleImage();

	// The panel in the world: its middle, facing the player's head (yaw only), upright; the eyes draw its corners from
	// here on
	const Vec2f size = image * s.metersPerPixel;
	const Vec3f middleWorld = above ? s.anchorWorld - Vec3f(0.f, size.y * 0.5f * params.unitsPerMeter, 0.f) : s.anchorWorld;
	Vec3f toEye = s.facingWorld - middleWorld;
	toEye.y = 0.f;
	const float toEyeLength = glm::length(toEye);
	const Vec3f facing = toEyeLength > 1e-3f ? toEye / toEyeLength : Vec3f(0.f, 0.f, -1.f);
	// The viewer's right along the panel (engine axes: x right, y down, z ahead)
	const Vec3f halfRight = Vec3f(-facing.z, 0.f, facing.x) * (size.x * 0.5f * params.unitsPerMeter);
	const Vec3f halfUp(0.f, -size.y * 0.5f * params.unitsPerMeter, 0.f);
	s.cornersWorld[0] = middleWorld - halfRight + halfUp;
	s.cornersWorld[1] = middleWorld + halfRight + halfUp;
	s.cornersWorld[2] = middleWorld + halfRight - halfUp;
	s.cornersWorld[3] = middleWorld - halfRight - halfUp;
	s.imageReady = true;

	// Its pose in the room this frame, for the status and the log: the head turning moves nothing
	const Vec3f middle = worldToLocal(middleWorld, head, headAngles);
	const Vec3f toEyeLocal = worldToLocal(s.facingWorld, head, headAngles) - middle;
	const float yaw = std::atan2(toEyeLocal.x, toEyeLocal.z);
	if(!s.panelLogged) {
		s.panelLogged = true;
		gameevent::Event("vr.subtitle.panel", 200)("drawn", subtitlePlacementName(s.drawn))("x", middle.x, 2)("y", middle.y, 2)
			("z", middle.z, 2)("yawDeg", glm::degrees(yaw), 1)("widthM", size.x, 2)("heightM", size.y, 2)
			("headX", head.x, 2)("headY", -head.y, 2)("headZ", -head.z, 2);
	}
	s.status.panelLocal = middle;
	s.status.panelYawDeg = glm::degrees(yaw);
	s.status.panelWidthM = size.x;
}
} // namespace vr
