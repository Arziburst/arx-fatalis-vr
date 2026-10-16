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

#ifndef ARX_INPUT_INPUTBACKEND_H
#define ARX_INPUT_INPUTBACKEND_H

#include <string>

#include "input/Keyboard.h"
#include "input/Mouse.h"
#include "math/Types.h"
#include "platform/Platform.h"


class TextInputHandler;
class Window;

class InputBackend {
	
public:
	
	virtual bool update() = 0;
	
	// Mouse
	virtual bool setMouseMode(Mouse::Mode mode) = 0;
	//! return true if the mouse position is currently being updated
	virtual bool getAbsoluteMouseCoords(int & absX, int & absY) const = 0;
	virtual void setAbsoluteMouseCoords(int absX, int absY) = 0;
	virtual void getRelativeMouseCoords(int & relX, int & relY, int & wheelDir) const = 0;
	virtual void getMouseButtonClickCount(int buttonId, int & _iNumClick, int & _iNumUnClick) const = 0;
	
	// Keyboard
	virtual bool isKeyboardKeyPressed(int keyId) const = 0;
	virtual void startTextInput(const Rect & box, TextInputHandler * handler) = 0;
	virtual void stopTextInput() = 0;
	virtual std::string getKeyName(Keyboard::Key key) const = 0;
	
	// The test channel (#338): mouse events as the window system delivers them; false when not supported
	virtual bool injectMouseMove(const Vec2i & /* motion */) { return false; }
	virtual bool injectMouseAt(const Vec2i & /* windowPos */) { return false; }
	virtual bool injectMouseButton(int /* buttonId */, bool /* down */) { return false; }
	virtual bool injectMouseWheel(int /* steps */) { return false; }
	virtual bool injectKey(int /* keyId */, bool /* down */) { return false; }

	// Real desktop input as the window system's own messages to the game window (#487): what the real
	// devices send, for tests that must prove it is dropped; false when not supported
	virtual bool postMouseAt(const Vec2i & /* windowPos */) { return false; }
	virtual bool postMouseButton(int /* buttonId */, bool /* down */) { return false; }
	virtual bool postMouseWheel(int /* steps */) { return false; }
	virtual bool postKey(int /* keyId */, bool /* down */) { return false; }

	//! Desktop input dropped before the game saw it (#487)
	struct DroppedInput {
		bool ignored = false; //!< dropped now
		u64 keys = 0;         //!< key presses
		u64 text = 0;         //!< text input events
		u64 buttons = 0;      //!< mouse button presses
		u64 wheel = 0;        //!< wheel steps
		u64 moves = 0;        //!< mouse motion events
	};
	//! The real mouse and keyboard in a test run's window (Window::isBackground): only injected events count
	virtual DroppedInput droppedRealInput() const { return { }; }
	//! The keyboard, real or injected, dropped while ignored (VR: the window only mirrors the headset)
	virtual void setKeyboardIgnored(bool /* ignored */) { }
	virtual DroppedInput droppedKeyboard() const { return { }; }

protected:
	
	virtual ~InputBackend() = default;
	
};

#endif // ARX_INPUT_INPUTBACKEND_H
