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

#ifndef ARX_GUI_BOOK_DEVPANEL_H
#define ARX_GUI_BOOK_DEVPANEL_H

#include <stddef.h>
#include <string_view>

#include "gui/DevVariants.h"
#include "math/Types.h"

/*!
 * The dev variants panel on the book's map page (#588): the game side of gui/DevVariants.h. It draws the panel
 * (or "No variants under test" with Show map) in a development launch and takes the clicks of its buttons -
 * the mouse, which in VR is the hand's reticle on the book (D-010, D-016). Runs only while the book shows its
 * map page: nothing per frame otherwise.
 */
namespace devvariants {

/*!
 * The map page's dev view, from MapPage::manage after the page's background
 * \param bookRect the book on the interface
 * \param scale    the book's scale
 * \return true when it drew the panel or the notice (the page draws no map), false for the map
 */
bool manageMapPage(const Rectf & bookRect, float scale);

//! The book closed: Show map is forgotten, the panel opens on its first page next time
void onBookClosed();

struct PanelStatus {
	MapPageView view = MapPageView::Map; //!< What the map page shows (in any launch, open book or not)
	bool visible = false;                //!< The book is open on its map page and shows the panel or the notice
	size_t page = 0;                     //!< The panel's page shown (0-based)
	size_t pages = 1;
};

//! For status.json devPanel.*
PanelStatus panelStatus();

const char * mapPageViewName(MapPageView view);

/*!
 * Where the button named \a target (devpanel.<group>.<index>, devpanel.<group>.minus | plus, devpanel.prev |
 * next | show_map) is on the interface while the panel is shown - the test channel's vr-point <hand> menu
 * \return false when the panel is not shown or has no such button on its page
 */
bool panelTargetRect(std::string_view target, Rectf & rect);

} // namespace devvariants

#endif // ARX_GUI_BOOK_DEVPANEL_H
