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

#ifndef ARX_GUI_LOADLEVELSCREEN_H
#define ARX_GUI_LOADLEVELSCREEN_H

#include "core/TimeTypes.h"
#include "graphics/BaseGraphicsTypes.h"


void progressBarReset();
void progressBarSetTotal(float total);
void progressBarAdvance(float delta = 1.f);

void LoadLevelScreen(AreaId area = { });
void LoadLevelScreenDestroy();

//! The loading screen's frames since progressBarReset() and the time spent drawing and presenting them (#440)
struct LoadingScreenStats {
	unsigned frames = 0;
	PlatformDuration drawTime = 0;
};
LoadingScreenStats loadingScreenStats();

#endif // ARX_GUI_LOADLEVELSCREEN_H
