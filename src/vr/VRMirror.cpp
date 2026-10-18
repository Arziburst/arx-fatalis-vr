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

#include "vr/VRMirror.h"

#if ARX_HAVE_OPENXR

#include <array>

#include "core/GameEvent.h"
#include "gui/Menu.h"
#include "io/log/Logger.h"
#include "vr/VRSwapchain.h"

namespace vr {

namespace {

/*!
 * The window's back buffer as the target of a 2D draw in normalized coordinates, with the renderer's
 * fixed-function state saved whole on construction and restored on destruction, so its cached state stays true
 */
class WindowDraw {

public:

	explicit WindowDraw(const Vec2i & windowSize) {

		glGetIntegerv(GL_ACTIVE_TEXTURE, &m_activeTexture);
		GLint units = 1;
		glGetIntegerv(GL_MAX_TEXTURE_UNITS, &units);
		glPushAttrib(GL_ALL_ATTRIB_BITS);

		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
		glDrawBuffer(GL_BACK);
		glViewport(0, 0, windowSize.x, windowSize.y);
		glDisable(GL_DEPTH_TEST);
		glDepthMask(GL_FALSE);
		glDisable(GL_STENCIL_TEST);
		glDisable(GL_BLEND);
		glDisable(GL_ALPHA_TEST);
		glDisable(GL_CULL_FACE);
		glDisable(GL_FOG);
		glDisable(GL_LIGHTING);
		glDisable(GL_SCISSOR_TEST);
		glDisable(GL_POLYGON_OFFSET_FILL);
		glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
		for(GLint unit = 1; unit < units; unit++) {
			glActiveTexture(GLenum(GL_TEXTURE0 + unit));
			glDisable(GL_TEXTURE_2D);
		}
		glActiveTexture(GL_TEXTURE0);
		glEnable(GL_TEXTURE_2D);
		glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		glColor4f(1.f, 1.f, 1.f, 1.f);

		glMatrixMode(GL_TEXTURE);
		glPushMatrix();
		glLoadIdentity();
		glMatrixMode(GL_PROJECTION);
		glPushMatrix();
		glLoadIdentity();
		glMatrixMode(GL_MODELVIEW);
		glPushMatrix();
		glLoadIdentity();
	}

	~WindowDraw() {

		glMatrixMode(GL_MODELVIEW);
		glPopMatrix();
		glMatrixMode(GL_PROJECTION);
		glPopMatrix();
		glMatrixMode(GL_TEXTURE);
		glPopMatrix();

		glPopAttrib();
		glActiveTexture(GLenum(m_activeTexture));
	}

	WindowDraw(const WindowDraw &) = delete;
	WindowDraw & operator=(const WindowDraw &) = delete;

private:

	GLint m_activeTexture = GL_TEXTURE0;

};

} // anonymous namespace

void DesktopMirror::begin(bool wanted, const char * why, size_t eye) {

	m_eye = eye == 0 ? 0 : 1;
	m_haveEye = false;
	m_haveFlat = false;
	m_logged = false;
	m_showsInterface = false;

	if(!wanted) {
		m_on = false;
		m_state = std::string("off: ") + why;
		LogInfo << "[vr] desktop mirror off (" << why << "): the window shows the flat frame";
		gameevent::Event("vr.mirror")("state", "off")("why", why);
		return;
	}

	// A raw copy between the sRGB swapchain format and our RGBA8 textures: no blit, which would
	// convert sRGB differently per driver, and no texture view of a runtime-owned texture
	OpenGLInfo gl;
	if(!gl.has("GL_ARB_copy_image", 4, 3)) {
		stop("OpenGL has no glCopyImageSubData (GL 4.3 / GL_ARB_copy_image)");
		return;
	}

	m_on = true;
	m_state = "on";
	LogInfo << "[vr] desktop mirror on: the window shows what the " << eyeName() << " eye sees (vr_mirror_eye "
	        << m_eye << ")";
	gameevent::Event("vr.mirror")("state", "on")("eye", eyeName());
}

void DesktopMirror::stop(const std::string & why) {
	m_on = false;
	m_state = "off: " + why;
	LogWarning << "[vr] desktop mirror off: " << why << " - the window shows the flat frame";
	gameevent::Event("vr.mirror")("state", "off")("why", why);
}

void DesktopMirror::destroy() {
	if(m_eyeTexture) {
		glDeleteTextures(1, &m_eyeTexture);
		m_eyeTexture = 0;
	}
	if(m_flatTexture) {
		glDeleteTextures(1, &m_flatTexture);
		m_flatTexture = 0;
	}
	for(GLuint & texture : m_overlayTextures) {
		if(texture) {
			glDeleteTextures(1, &texture);
			texture = 0;
		}
	}
	m_eyeSize = Vec2i(0);
	m_flatSize = Vec2i(0);
	m_haveEye = false;
	m_haveFlat = false;
	if(m_on) {
		m_on = false;
		m_state = "off: the VR session ended";
	}
}

bool DesktopMirror::prepareTexture(GLuint & texture, Vec2i & size, const Vec2i & wanted) {

	if(texture && size == wanted) {
		return true;
	}

	if(!texture) {
		glGenTextures(1, &texture);
	}

	// The caller (GLStateGuard in VRSystem) restores the texture binding
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, wanted.x, wanted.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
	size = wanted;

	return true;
}

void DesktopMirror::copyEye(const Swapchain & eye) {

	if(!m_on) {
		return;
	}

	prepareTexture(m_eyeTexture, m_eyeSize, eye.size());
	glCopyImageSubData(eye.texture(), GL_TEXTURE_2D, 0, 0, 0, 0,
	                   m_eyeTexture, GL_TEXTURE_2D, 0, 0, 0, 0, m_eyeSize.x, m_eyeSize.y, 1);
	m_haveEye = true;
}

void DesktopMirror::copyFlatFrame(const Swapchain & quad) {

	if(!m_on) {
		return;
	}

	prepareTexture(m_flatTexture, m_flatSize, quad.size());
	glCopyImageSubData(quad.texture(), GL_TEXTURE_2D, 0, 0, 0, 0,
	                   m_flatTexture, GL_TEXTURE_2D, 0, 0, 0, 0, m_flatSize.x, m_flatSize.y, 1);
	m_haveFlat = true;
}

void DesktopMirror::setOverlayImage(OverlayImage image, const Vec2i & size, const u8 * rgba) {

	if(!m_on) {
		return;
	}

	GLuint & texture = m_overlayTextures[image];
	if(!texture) {
		glGenTextures(1, &texture);
	}

	// The caller restores the texture binding, as for the other textures of the mirror. The engine's tight
	// GL_UNPACK_ALIGNMENT (1) is kept: RGBA rows are a multiple of 4 anyway, and the 4 once set here leaked
	// into every later upload and crashed on a cinematic tile (#495, TRAPS.md)
	glBindTexture(GL_TEXTURE_2D, texture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
}

void DesktopMirror::draw(const Vec2i & windowSize, const MirrorFov & fov, const Vec3f & eyePosition,
                         const glm::quat & eyeOrientation, const Quad & quad, const std::vector<Overlay> & overlays,
                         const Vec3f & overlayEyePosition, const glm::quat & overlayEyeOrientation) {

	bool haveEye = m_haveEye;
	const bool flatCopied = m_haveFlat;
	bool haveFlat = m_haveFlat && quad.shown;
	m_haveEye = false;
	m_haveFlat = false;
	if(!m_on || windowSize.x <= 0 || windowSize.y <= 0) {
		return;
	}

	// The main and pause menu, the character creation and the credits have no world around them: the
	// window shows the 2D interface itself, filling it - where the desktop mouse lands, since the mouse maps
	// the whole window onto the interface (#334). In the headset they are still the floating panel (#49)
	const bool interfaceOnly = ARXmenu.mode() != Mode_InGame && flatCopied;
	if(interfaceOnly != m_showsInterface) {
		m_showsInterface = interfaceOnly;
		if(interfaceOnly) {
			LogInfo << "[vr] desktop mirror: a menu without the world - the window shows the 2D interface "
			        << m_flatSize.x << 'x' << m_flatSize.y << " filling it";
		} else {
			LogInfo << "[vr] desktop mirror: the window shows the " << eyeName() << " eye again";
		}
	}
	if(interfaceOnly) {
		drawInterface(windowSize, quad.blended);
		m_frames++;
		m_overlaysDrawn = 0;
		return;
	}

	if(!haveEye) {
		return;
	}

	MirrorRect crop = mirrorCrop(fov, m_eyeSize, windowSize);

	// Where the quad appears in the mirrored eye: its corners projected, each with its depth for a
	// perspective-correct texture (q = 1 / depth); a corner behind the eye leaves the quad out
	struct Corner {
		Vec2f window;
		float s, t, q;
	};
	typedef std::array<Corner, 4> Corners;
	const auto project = [&](const Vec3f & position, const glm::quat & orientation, float width, float height,
	                         const Vec3f & eye, const glm::quat & eyeRotation, Corners & out) {
		const float x = width * 0.5f, y = height * 0.5f;
		const Vec3f points[4] = {
			position + orientation * Vec3f(-x, -y, 0.f), position + orientation * Vec3f(x, -y, 0.f),
			position + orientation * Vec3f(-x, y, 0.f), position + orientation * Vec3f(x, y, 0.f),
		};
		for(size_t i = 0; i < out.size(); i++) {
			Vec2f uv(0.f);
			float depth = 0.f;
			if(!mirrorProject(points[i], eye, eyeRotation, fov, uv, depth)) {
				return false;
			}
			out[i].window = mirrorToWindow(crop, uv);
			out[i].q = 1.f / depth;
			out[i].s = float(i % 2) * out[i].q;
			out[i].t = float(i / 2) * out[i].q;
		}
		return true;
	};
	Corners corners;
	if(haveFlat) {
		haveFlat = project(quad.position, quad.orientation, quad.width, quad.height, eyePosition, eyeOrientation,
		                   corners);
	}

	// The quad layers after the flat frame, in submission order; one with a corner behind the eye is left out
	struct PlacedOverlay {
		GLuint texture;
		Corners corners;
	};
	std::vector<PlacedOverlay> placed;
	for(const Overlay & overlay : overlays) {
		PlacedOverlay entry;
		entry.texture = m_overlayTextures[overlay.image];
		if(entry.texture && project(overlay.position, overlay.orientation, overlay.width, overlay.height,
		                            overlayEyePosition, overlayEyeOrientation, entry.corners)) {
			placed.push_back(entry);
		}
	}

	if(placed.size() != m_overlaysDrawn) {
		LogInfo << "[vr] desktop mirror: " << placed.size() << " of " << overlays.size()
		        << " quad layer(s) after the flat frame in view of the window";
		for(const PlacedOverlay & overlay : placed) {
			LogInfo << "[vr] desktop mirror: overlay from (" << overlay.corners[0].window.x << ", "
			        << overlay.corners[0].window.y << ") to (" << overlay.corners[3].window.x << ", "
			        << overlay.corners[3].window.y << ") (-1..1)";
		}
	}

	if(!m_logged) {
		m_logged = true;
		LogInfo << "[vr] desktop mirror: " << eyeName() << " eye " << m_eyeSize.x << 'x' << m_eyeSize.y << ", crop u "
		        << crop.u0 << ".." << crop.u1 << " v " << crop.v0 << ".." << crop.v1 << " into the window "
		        << windowSize.x << 'x' << windowSize.y
		        << (haveFlat ? "" : ", no flat frame quad");
		if(haveFlat) {
			LogInfo << "[vr] desktop mirror: flat frame quad in the window from (" << corners[0].window.x << ", "
			        << corners[0].window.y << ") to (" << corners[3].window.x << ", " << corners[3].window.y
			        << ") (-1..1)";
		}
	}

	// The fixed-function state of the renderer, saved whole and restored when this function returns
	const WindowDraw state(windowSize);

	// The mirrored eye, cropped to the window
	glBindTexture(GL_TEXTURE_2D, m_eyeTexture);
	glBegin(GL_TRIANGLE_STRIP);
	glTexCoord2f(crop.u0, crop.v0);
	glVertex2f(-1.f, -1.f);
	glTexCoord2f(crop.u1, crop.v0);
	glVertex2f(1.f, -1.f);
	glTexCoord2f(crop.u0, crop.v1);
	glVertex2f(-1.f, 1.f);
	glTexCoord2f(crop.u1, crop.v1);
	glVertex2f(1.f, 1.f);
	glEnd();

	// The flat frame where the mirrored eye sees the quad; the 2D interface drawn into it (#19) is
	// premultiplied by its alpha and shows the eye through where nothing was drawn
	if(haveFlat) {
		if(quad.blended) {
			glEnable(GL_BLEND);
			glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		}
		glBindTexture(GL_TEXTURE_2D, m_flatTexture);
		glBegin(GL_TRIANGLE_STRIP);
		for(const Corner & corner : corners) {
			glTexCoord4f(corner.s, corner.t, 0.f, corner.q);
			glVertex2f(corner.window.x, corner.window.y);
		}
		glEnd();
		if(quad.blended) {
			glDisable(GL_BLEND);
		}
	}

	// The beam and the reticles over it: premultiplied by their alpha like the interface
	if(!placed.empty()) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
		for(const PlacedOverlay & overlay : placed) {
			glBindTexture(GL_TEXTURE_2D, overlay.texture);
			glBegin(GL_TRIANGLE_STRIP);
			for(const Corner & corner : overlay.corners) {
				glTexCoord4f(corner.s, corner.t, 0.f, corner.q);
				glVertex2f(corner.window.x, corner.window.y);
			}
			glEnd();
		}
		glDisable(GL_BLEND);
	}

	m_frames++;
	m_overlaysDrawn = placed.size();
}

void DesktopMirror::drawInterface(const Vec2i & windowSize, bool blended) {

	const WindowDraw state(windowSize);

	// Over black: where the interface drew nothing a blended flat frame (#19) is transparent
	const GLfloat black[4] = { 0.f, 0.f, 0.f, 1.f };
	glClearBufferfv(GL_COLOR, 0, black);
	if(blended) {
		glEnable(GL_BLEND);
		glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	}

	// Stretched over the whole window, as the mouse is mapped: the interface has the window's aspect, but
	// for the frame after a resize, before the interface is laid out again
	glBindTexture(GL_TEXTURE_2D, m_flatTexture);
	glBegin(GL_TRIANGLE_STRIP);
	glTexCoord2f(0.f, 0.f);
	glVertex2f(-1.f, -1.f);
	glTexCoord2f(1.f, 0.f);
	glVertex2f(1.f, -1.f);
	glTexCoord2f(0.f, 1.f);
	glVertex2f(-1.f, 1.f);
	glTexCoord2f(1.f, 1.f);
	glVertex2f(1.f, 1.f);
	glEnd();
}

} // namespace vr

#endif // ARX_HAVE_OPENXR
