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

#include "gui/DamageNumbers.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/Core.h"
#include "core/GameEvent.h"
#include "core/GameTime.h"
#include "game/Camera.h"
#include "game/Entity.h"
#include "game/EntityManager.h"
#include "game/Levels.h"
#include "graphics/Color.h"
#include "graphics/data/Mesh.h"
#include "graphics/font/Font.h"
#include "graphics/font/FontCache.h"
#include "io/log/Logger.h"
#include "vr/WorldScale.h"

namespace damagenumbers {

namespace {

//! Hits on one target this soon after its number appeared add to that number (damage every frame)
const GameDuration MergeWindow = 250ms;
//! A number rises for this long, then it is gone
const GameDuration Lifetime = 1200ms;
//! It is fully opaque until then and fades out after
const GameDuration FadeStart = 600ms;

//! It appears this far above the top of the target's cylinder (meters) and rises this much
const float AboveHeadMeters = 0.2f;
const float RiseMeters = 0.5f;

//! The font's size as the angle it covers in the view, so it reads the same near and far, in a
//! headset as on a monitor (the digits are about 70 % of it)
const float FontSizeDeg = 4.f;
const unsigned MinFontPixels = 12;
const unsigned MaxFontPixels = 256;
//! A sans-serif with plain digits, emboldened (FontCache weight)
const char * const FontFile = "misc/dejavusansmono.ttf";
const unsigned FontWeight = 3;

//! One plain warm yellow on a black outline: readable on the dark levels and against a lit wall
const Color3f TextColor = Color3f(1.f, 0.86f, 0.3f);
const Color3f OutlineColor = Color3f(0.f, 0.f, 0.f);
const float OutlineAlpha = 0.85f;

//! A fight in the playground never shows more at once; the oldest goes first
const size_t MaxNumbers = 32;

struct DamageNumber {
	EntityHandle target;
	std::string targetName;
	Vec3f anchor = Vec3f(0.f);  //!< above the target's head when it was last seen
	GameInstant start;
	float damage = 0.f;
	float dealt = 0.f;
	u32 hits = 0;
	bool logged = false;
	//! Where the last frame drew it, per view (each eye in VR)
	size_t views = 0;
	std::array<Vec2f, 2> screen = { { Vec2f(0.f), Vec2f(0.f) } };
};

std::vector<DamageNumber> g_numbers;
//! Scene views drawn in this frame
size_t g_view = 0;
Font * g_font = nullptr;

Vec3f aboveHead(const Entity & entity) {
	// The cylinder's height is negative: up is -y
	return entity.pos + Vec3f(0.f, entity.physics.cyl.height - AboveHeadMeters * vr::UnitsPerMeter, 0.f);
}

const Entity * targetOf(const DamageNumber & number) {
	const Entity * entity = entities.get(number.target);
	return (entity && entity->idString() == number.targetName) ? entity : nullptr;
}

std::string format(float damage) {
	char text[32];
	if(damage < 9.95f) {
		std::snprintf(text, sizeof(text), "%.1f", double(damage));
	} else {
		std::snprintf(text, sizeof(text), "%.0f", double(damage));
	}
	return text;
}

void log(const DamageNumber & number) {
	gameevent::Event event("damage.number");
	event("target", number.targetName)("damage", number.damage, 1)("dealt", number.dealt, 1)("hits", number.hits);
	event("views", number.views);
	if(number.views > 0) {
		event("x0", s32(number.screen[0].x))("y0", s32(number.screen[0].y));
	}
	if(number.views > 1) {
		event("x1", s32(number.screen[1].x))("y1", s32(number.screen[1].y));
	}
}

//! The font for the current view: the pixels of \ref FontSizeDeg in it
Font * fontForView() {

	float pixels = g_preparedCamera.m_pixelsPerUnit * std::tan(glm::radians(FontSizeDeg));
	unsigned size = std::clamp(unsigned(std::lround(pixels)), MinFontPixels, MaxFontPixels);
	if(g_font && g_font->getSize() == size) {
		return g_font;
	}

	Font * font = FontCache::getFont(FontFile, size, FontWeight);
	if(!font) {
		LogError << "Could not load the damage number font " << FontFile << " at size " << size;
	}
	FontCache::releaseFont(g_font);
	g_font = font;
	return g_font;
}

void drawOutlined(Font & font, Vec2f center, const std::string & text, float alpha) {

	Vec2i size = font.getTextSize(text);
	Vec2i corner = Vec2i(center - Vec2f(size) / 2.f);

	s32 outline = std::max(s32(font.getSize() / 14), s32(1));
	Color outlineColor = Color(Color4f(OutlineColor, OutlineAlpha * alpha));
	for(s32 y = -1; y <= 1; y++) {
		for(s32 x = -1; x <= 1; x++) {
			if(x != 0 || y != 0) {
				font.draw(corner + Vec2i(x, y) * outline, text, outlineColor);
			}
		}
	}

	font.draw(corner, text, Color(Color4f(TextColor, alpha)));

}

} // anonymous namespace

void hit(const Entity & target, float damage, float dealt) {

	if(!isPlaygroundArea(g_currentArea) || damage <= 0.f) {
		return;
	}

	GameInstant now = g_gameTime.now();
	for(DamageNumber & number : g_numbers) {
		if(number.target == target.index() && !number.logged && now - number.start < MergeWindow) {
			number.damage += damage;
			number.dealt += dealt;
			number.hits++;
			return;
		}
	}

	if(g_numbers.size() >= MaxNumbers) {
		if(!g_numbers.front().logged) {
			log(g_numbers.front());
		}
		g_numbers.erase(g_numbers.begin());
	}

	DamageNumber & number = g_numbers.emplace_back();
	number.target = target.index();
	number.targetName = target.idString();
	number.anchor = aboveHead(target);
	number.start = now;
	number.damage = damage;
	number.dealt = dealt;
	number.hits = 1;

}

void update() {

	g_view = 0;

	GameInstant now = g_gameTime.now();
	for(DamageNumber & number : g_numbers) {
		if(const Entity * entity = targetOf(number)) {
			number.anchor = aboveHead(*entity);
		}
		if(!number.logged && now - number.start >= MergeWindow) {
			log(number);
			number.logged = true;
		}
	}

	g_numbers.erase(std::remove_if(g_numbers.begin(), g_numbers.end(), [now](const DamageNumber & number) {
		return number.logged && now - number.start >= Lifetime;
	}), g_numbers.end());

}

void render() {

	size_t view = g_view++;
	if(g_numbers.empty()) {
		return;
	}

	Font * font = fontForView();
	if(!font) {
		return;
	}

	GameInstant now = g_gameTime.now();
	for(DamageNumber & number : g_numbers) {

		GameDuration age = now - number.start;
		float progress = std::clamp(age / Lifetime, 0.f, 1.f);
		float alpha = 1.f;
		if(age > FadeStart) {
			alpha = std::clamp(1.f - (age - FadeStart) / (Lifetime - FadeStart), 0.f, 1.f);
		}
		if(alpha <= 0.f) {
			continue;
		}

		Vec3f pos = number.anchor - Vec3f(0.f, RiseMeters * vr::UnitsPerMeter * progress, 0.f);
		Vec4f clip = worldToClipSpace(pos);
		if(clip.w <= 0.f) {
			// Behind this view
			continue;
		}
		Vec2f center = Vec2f(clip) / clip.w;

		if(view < number.screen.size()) {
			number.screen[view] = center;
		}
		number.views = view + 1;

		drawOutlined(*font, center, format(number.damage), alpha);

	}

}

void clear() {
	g_numbers.clear();
}

void releaseFont() {
	FontCache::releaseFont(g_font);
	g_font = nullptr;
}

} // namespace damagenumbers
