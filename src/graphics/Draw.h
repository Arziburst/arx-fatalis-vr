/*
 * Copyright 2011-2019 Arx Libertatis Team (see the AUTHORS file)
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

#ifndef ARX_GRAPHICS_DRAW_H
#define ARX_GRAPHICS_DRAW_H

#include "graphics/RenderBatcher.h"
#include "graphics/Renderer.h"
#include "graphics/Vertex.h"
#include "graphics/texture/TextureStage.h"
#include "graphics/data/TextureContainer.h"
#include "math/Types.h"

struct EERIEPOLY;
struct EERIE_3DOBJ;
struct Sphere;
struct Cylinder;

void EERIEDRAWPRIM(Renderer::Primitive primitive, const TexturedVertex * vertices, size_t count = 3, bool nocount = false);

void EERIEDrawBitmap(const Rectf & rect, float z, TextureContainer * tex, Color color);
void EERIEDrawBitmap2DecalY(Rectf rect, float z, TextureContainer * tex, Color col, float _fDeltaY);

void EERIEDrawSprite(const Vec3f & in, float siz, TextureContainer * tex, Color col, float Zpos);

void EERIEDrawBitmap_uv(Rectf rect, float z, TextureContainer * tex, Color col, float u0, float v0, float u1, float v1);
void EERIEDrawBitmapUVs(Rectf rect, float z, TextureContainer * tex, Color color, Vec2f uv0, Vec2f uv1, Vec2f uv2, Vec2f uv3);

void EERIEAddBitmap(const RenderMaterial & mat, const Vec3f & p, float sx, float sy, TextureContainer * tex, Color color);
void EERIEAddSprite(const RenderMaterial & mat, const Vec3f & in, float siz, Color color, float Zpos, float rot = 0);

/*!
 * The head between the eyes while the eyes of a stereo frame are drawn (#22, vr/StereoEffects.h):
 * camera-facing geometry - sprites, trails - faces it, so that both eyes see one shape in the world,
 * and full-view overlays hang from it at infinity. Set by ArxGame::renderStereoViews(); inactive in
 * flat mode and outside the eye passes, where everything is drawn as the engine always drew it.
 */
struct StereoView {
	bool active = false;
	//! The test channel's "stereo-fx legacy on": the eyes draw these effects the old, per-eye way -
	//! the before picture, so a check can show that it fails without the fix
	bool legacy = false;
	//! The test channel's "stereo-fx overlays off": the full-view overlays are not drawn, so that a shot with them
	//! and one without differ by the overlay alone (Magic Sight also tints the world)
	bool overlaysHidden = false;
	glm::mat4x4 worldToView = glm::mat4x4(1.f); //!< the head camera's (engine view axes)
	glm::mat4x4 viewToWorld = glm::mat4x4(1.f);
	Vec3f position = Vec3f(0.f); //!< the point between the eyes, in the world
	float focal = 0.f;           //!< the head camera's focal length: the scale of fixed-share sprites
	//! The union of both eyes' frusta in the head's axes, as OpenXR angles in radians (left, right, up, down)
	Vec4f unionFov = Vec4f(0.f);
};

extern StereoView g_stereoView;

//! \return where camera-facing geometry looks from: the head between the eyes in a stereo frame, else the camera
Vec3f viewerPosition();

/*!
 * Draw a texture or a color over the whole view, as the engine's full-screen effects do (the
 * Improve Vision tint, Magic Sight): in flat mode the bitmap over \c g_size; in an eye of a stereo
 * frame a head-locked image at infinity over both eyes' union frustum, so every texel is one
 * direction for both eyes (#22). The caller sets the render state.
 */
void drawViewOverlay(float z, TextureContainer * tex, Color color);

#endif // ARX_GRAPHICS_DRAW_H
