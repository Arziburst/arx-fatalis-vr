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

#ifndef ARX_CORE_SAVEGAME_H
#define ARX_CORE_SAVEGAME_H

#include <stddef.h>
#include <vector>
#include <string>
#include <string_view>
#include <ctime>

#include <boost/iterator/counting_iterator.hpp>
#include <boost/iterator/transform_iterator.hpp>

#include "core/SaveKind.h"
#include "graphics/BaseGraphicsTypes.h"
#include "graphics/image/Image.h"
#include "io/fs/FilePath.h"
#include "io/resource/ResourcePath.h"
#include "util/HandleType.h"


typedef util::HandleType<struct SavegameHandleTag, long> SavegameHandle;

extern const fs::path SAVEGAME_NAME;

struct SaveGame {
	
	bool quicksave = false;
	
	std::string name;
	
	fs::path savefile;
	res::path thumbnail;
	
	AreaId area;
	std::time_t stime = 0;
	
	SaveKind kind = SaveKind::Flat;
	
};

/*!
 * Central management of the list of savegames.
 *
 * Lists only the saves of this run's kind (#417, D-NEW, core/SaveKind.h): a VR run never sees a flat save and a flat
 * run never a VR one - the menu, quick load, \c --loadslot and the channel's \c loadslot all index this list. The
 * saves of the other kind stay on disk, counted by \ref hiddenCount.
 */
class SaveGameList {
	
	struct IndexToHandle {
		typedef SavegameHandle result_type;
		SavegameHandle operator()(size_t index) const {
			return SavegameHandle(long(index));
		}
	};
	
	typedef boost::counting_iterator<size_t> iterator_base;
	
public:
	
	typedef boost::transform_iterator<IndexToHandle, iterator_base> iterator;
	typedef boost::transform_iterator<IndexToHandle, iterator_base> const_iterator;
	
	//! Update the savegame list. This is automatically called by save() and remove()
	void update(bool verbose = false);
	
	//! The kind of save this run writes, lists and loads: set once, before the first \ref update
	void setKind(SaveKind kind) { m_kind = kind; }
	SaveKind kind() const { return m_kind; }
	
	//! Saves in the save folder that belong to the other kind and are not listed
	size_t hiddenCount() const { return m_hidden.size(); }
	
	/*!
	 * Why a load asked for by slot finds nothing to load: \a slot is past the list, and saves of the other kind are
	 * hidden - an empty string when no save is hidden
	 */
	std::string hiddenReason() const;
	
	/*!
	 * Check a save file that does not come from this list (a path on the command line, a file dropped on the window)
	 * \return why this run must not load it, or an empty string
	 */
	std::string refusal(const fs::path & savefile) const;
	
	/*! Save the current game state
	 * \param name The name of the new savegame.
	 * \param overwrite A savegame to overwrite with this save or SavegameHandle()
	 * \return true if the game was successfully saved.
	 */
	bool save(std::string_view name, SavegameHandle overwrite = SavegameHandle(),
	          const Image & thumbnail = Image());
	
	//! Perform a quicksave: Maintain a number of quicksave slots and always overwrite the oldest one.
	bool quicksave(const Image & thumbnail = Image());
	
	//! Return the newest savegame or end() if there is no savegame.
	SavegameHandle quickload();
	
	//! Delete the given savegame. This removes the actual on-disk files.
	void remove(SavegameHandle handle);
	
	iterator begin() const { return iterator(iterator_base(0), IndexToHandle()); }
	iterator end() const { return iterator(iterator_base(size()), IndexToHandle()); }
	
	size_t size() const { return savelist.size(); }
	bool empty() const { return savelist.empty(); }
	const SaveGame & operator[](SavegameHandle handle) const { return savelist[size_t(handle.handleData())]; }
	
private:
	
	//! A save of the other kind: remembered so it is read and logged once, not on every update
	struct HiddenSave {
		fs::path savefile;
		std::time_t stime = 0;
		SaveKind kind = SaveKind::Flat;
	};
	
	std::vector<SaveGame> savelist;
	std::vector<HiddenSave> m_hidden;
	SaveKind m_kind = SaveKind::Flat;
	
};

extern SaveGameList savegames;

#endif // ARX_CORE_SAVEGAME_H
