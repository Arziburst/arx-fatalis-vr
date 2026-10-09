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

/*
 * The runtime interface's default implementation (#112): the OpenXR loader's own functions.
 */

#include "vr/OpenXRUtil.h"

#if ARX_HAVE_OPENXR

#include <type_traits>

/*
 * The loader's exports, declared from the function pointer types: the headers' own prototypes are
 * hidden in the VR module (XR_NO_PROTOTYPES), and a unity build sees this file after the others.
 * Same declaration as the headers' (no dllimport there either), so it links the same way.
 */
#define ARX_XR_FUNCTION(name) extern "C" std::remove_pointer_t<PFN_##name> name;
#include "vr/XRFunctions.h"
#undef ARX_XR_FUNCTION

namespace vr {

#define ARX_XR_FUNCTION(name) PFN_##name name = &::name;
#include "vr/XRFunctions.h"
#undef ARX_XR_FUNCTION

} // namespace vr

#endif // ARX_HAVE_OPENXR
