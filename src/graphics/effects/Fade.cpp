/*
 * Copyright 2013-2022 Arx Libertatis Team (see the AUTHORS file)
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

#include "graphics/effects/Fade.h"

#include "core/Core.h"
#include "core/GameTime.h"

#include "graphics/Draw.h"
#include "graphics/Renderer.h"

#include "vr/VRSystem.h"

static PlatformDuration FADEDURATION = 0;
long FADEDIR = 0;
static PlatformInstant FADESTART = 0;
float LAST_FADEVALUE = 1.f;
static Color3f FADECOLOR;


void fadeReset() {
	FADEDIR = 0;
	FADEDURATION = 0;
	FADESTART = 0;
	FADECOLOR = Color3f::black;
}

void fadeSetColor(Color3f color) {
	FADECOLOR = color;
}

void fadeRequestStart(FadeType type, const PlatformDuration duration) {
	switch(type) {
		case FadeType_In:
			FADEDIR = 1;
			break;
		case FadeType_Out:
			FADEDIR = -1;
			break;
	}
	
	FADEDURATION = duration;
	FADESTART = g_platformTime.frameStart();
}

//! The fade's opacity this frame; false before it starts, below 0 once it has run out
static bool fadeVisibility(float & visibility) {
	
	PlatformDuration elapsed = g_platformTime.frameStart() - FADESTART;
	if(elapsed < 0)
		return false;

	visibility = elapsed / FADEDURATION;

	if(FADEDIR > 0)
		visibility = 1.f - visibility;

	if(visibility > 1.f)
		visibility = 1.f;

	return true;
}

void ManageFade() {
	
	float Visibility;
	if(!fadeVisibility(Visibility))
		return;

	if(Visibility < 0.f) {
		FADEDIR = 0;
		return;
	}
	
	LAST_FADEVALUE = Visibility;
	
	if(vr::stereoActive() && !g_stereoView.legacy) {
		// The eyes were faded over their whole view (drawFadeOverView). The flat frame is a quad in the
		// middle of the view: faded like the window it would darken that middle twice and leave the rest
		// bright. Only the interface drawn on it fades, and it covers no more of the eyes than it did (#22)
		UseRenderState state(render2D().blend(BlendDstAlpha, BlendInvSrcAlpha));
		EERIEDrawBitmap(Rectf(g_size), 0.0001f, nullptr, Color(Color4f(FADECOLOR * Visibility, Visibility)));
		return;
	}
	
	UseRenderState state(render2D());
	
	EERIEDrawBitmap(Rectf(g_size), 0.0001f, nullptr, Color(Color4f(FADECOLOR, Visibility)));
	
}

void drawFadeOverView() {
	
	float visibility;
	if(!FADEDIR || !fadeVisibility(visibility) || visibility <= 0.f) {
		return;
	}

	UseRenderState state(render2D());
	EERIEDrawBitmap(Rectf(g_size), 0.0001f, nullptr, Color(Color4f(FADECOLOR, visibility)));
}
