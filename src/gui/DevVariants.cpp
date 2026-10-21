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

#include "gui/DevVariants.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>

#include "io/log/Logger.h"

namespace devvariants {

namespace {

struct Item {
	Entry entry;
	std::function<void(size_t)> choiceSetter;
	std::function<void(float)> numberSetter;
};

std::vector<Item> g_items;

Item * findItem(std::string_view group) {
	for(Item & item : g_items) {
		if(item.entry.group == group) {
			return &item;
		}
	}
	return nullptr;
}

bool validId(std::string_view group) {
	if(group.empty()) {
		return false;
	}
	return std::none_of(group.begin(), group.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)); });
}

//! Adds the item, or replaces the group of the same id in its place
void store(Item && item) {
	if(Item * existing = findItem(item.entry.group)) {
		*existing = std::move(item);
	} else {
		g_items.push_back(std::move(item));
	}
}

std::string numberText(float value, std::string_view unit) {
	std::ostringstream oss;
	oss.imbue(std::locale::classic());
	oss << std::fixed << std::setprecision(3) << double(value);
	std::string text = oss.str();
	if(text.find('.') != std::string::npos) {
		text.erase(text.find_last_not_of('0') + 1);
		if(text.back() == '.') {
			text.pop_back();
		}
	}
	if(text == "-0") {
		text = "0";
	}
	if(!unit.empty()) {
		text += ' ';
		text += unit;
	}
	return text;
}

std::string entryText(const Entry & entry) {
	if(entry.kind == Kind::Choice) {
		return entry.current < entry.choices.size() ? entry.choices[entry.current] : std::string();
	}
	return numberText(entry.value, entry.unit);
}

void applyChoice(Item & item, size_t index) {
	item.entry.current = index;
	if(item.choiceSetter) {
		item.choiceSetter(index);
	}
	LogInfo << "[dev] variant " << item.entry.group << " = " << entryText(item.entry);
}

//! \return false when the value stays as it is
bool applyNumber(Item & item, float value) {
	value = std::clamp(value, item.entry.min, item.entry.max);
	if(value == item.entry.value) {
		return false;
	}
	item.entry.value = value;
	if(item.numberSetter) {
		item.numberSetter(value);
	}
	LogInfo << "[dev] variant " << item.entry.group << " = " << entryText(item.entry);
	return true;
}

bool parseNumber(std::string_view text, float & value) {
	std::istringstream iss{std::string(text)};
	iss.imbue(std::locale::classic());
	double number = 0.0;
	iss >> number;
	if(!iss || !std::isfinite(number)) {
		return false;
	}
	iss >> std::ws;
	if(!iss.eof()) {
		return false;
	}
	value = float(number);
	return true;
}

} // anonymous namespace

bool registerChoice(std::string_view group, std::string_view prLabel, std::vector<std::string> choices,
                    size_t defaultIndex, std::function<void(size_t)> setter, bool afterRestart) {
	if(!validId(group) || choices.empty() || defaultIndex >= choices.size()) {
		return false;
	}
	Item item;
	item.entry.group = group;
	item.entry.prLabel = prLabel;
	item.entry.kind = Kind::Choice;
	item.entry.choices = std::move(choices);
	item.entry.current = defaultIndex;
	item.entry.afterRestart = afterRestart;
	item.choiceSetter = std::move(setter);
	store(std::move(item));
	return true;
}

bool registerNumber(std::string_view group, std::string_view prLabel, float min, float max, float step,
                    std::string_view unit, float defaultValue, std::function<void(float)> setter,
                    bool afterRestart) {
	if(!validId(group) || !(min < max) || !(step > 0.f)) {
		return false;
	}
	Item item;
	item.entry.group = group;
	item.entry.prLabel = prLabel;
	item.entry.kind = Kind::Number;
	item.entry.min = min;
	item.entry.max = max;
	item.entry.step = step;
	item.entry.unit = unit;
	item.entry.value = std::clamp(defaultValue, min, max);
	item.entry.afterRestart = afterRestart;
	item.numberSetter = std::move(setter);
	store(std::move(item));
	return true;
}

bool unregister(std::string_view group) {
	auto it = std::find_if(g_items.begin(), g_items.end(), [group](const Item & item) {
		return item.entry.group == group;
	});
	if(it == g_items.end()) {
		return false;
	}
	g_items.erase(it);
	return true;
}

void clear() {
	g_items.clear();
}

std::vector<Entry> entries() {
	std::vector<Entry> list;
	list.reserve(g_items.size());
	for(const Item & item : g_items) {
		list.push_back(item.entry);
	}
	return list;
}

size_t count() {
	return g_items.size();
}

const Entry * find(std::string_view group) {
	const Item * item = findItem(group);
	return item ? &item->entry : nullptr;
}

bool choose(std::string_view group, size_t index) {
	Item * item = findItem(group);
	if(!item || item->entry.kind != Kind::Choice || index >= item->entry.choices.size()
	   || index == item->entry.current) {
		return false;
	}
	applyChoice(*item, index);
	return true;
}

bool step(std::string_view group, int direction) {
	Item * item = findItem(group);
	if(!item || item->entry.kind != Kind::Number || direction == 0) {
		return false;
	}
	const Entry & entry = item->entry;
	// The next point of the grid min + k * step in double, so that repeated steps do not drift; a value off the
	// grid (a set) goes to the grid point next to it in the direction
	const double min = entry.min;
	const double size = entry.step;
	const double steps = (double(entry.value) - min) / size;
	const double epsilon = 1e-4;
	const double k = (direction > 0) ? std::floor(steps + epsilon) + 1.0 : std::ceil(steps - epsilon) - 1.0;
	double next = min + k * size;
	if(next >= double(entry.max) - size * epsilon) {
		next = entry.max;
	} else if(next <= min + size * epsilon) {
		next = min;
	}
	return applyNumber(*item, float(next));
}

bool set(std::string_view group, std::string_view value) {
	Item * item = findItem(group);
	if(!item) {
		return false;
	}
	if(item->entry.kind == Kind::Choice) {
		const std::vector<std::string> & choices = item->entry.choices;
		auto it = std::find(choices.begin(), choices.end(), value);
		if(it == choices.end()) {
			return false;
		}
		return choose(group, size_t(it - choices.begin()));
	}
	float number = 0.f;
	if(!parseNumber(value, number)) {
		return false;
	}
	return applyNumber(*item, number);
}

std::string valueText(std::string_view group) {
	const Entry * entry = find(group);
	return entry ? entryText(*entry) : std::string();
}

MapPageView mapPageView(bool devLaunch, size_t groups, bool showMap) {
	if(!devLaunch) {
		return MapPageView::Map;
	}
	if(groups > 0) {
		return MapPageView::Panel;
	}
	return showMap ? MapPageView::Map : MapPageView::Empty;
}

namespace {

//! The sizes of the panel, all from the row height
struct Metrics {
	Rectf area;
	float row = 0.f;
	float gap = 0.f;     //!< Between rows and between buttons in a row
	float padding = 0.f; //!< Around a button's text
	TextWidth textWidth;

	float buttonWidth(std::string_view label) const {
		return std::min(std::max(row, textWidth(label) + 2.f * padding), area.width());
	}
};

/*!
 * Places a group's label and its buttons at \a top, in panel coordinates of \a metrics.area
 * \return the bottom of the group's last row
 */
float placeGroup(const Entry & entry, const Metrics & metrics, float top, size_t block, GroupLayout & group,
                 std::vector<Button> & buttons) {

	const Rectf & area = metrics.area;
	const float row = metrics.row;

	group.group = entry.group;
	group.block = block;
	group.label = Rectf(area.left, top, area.right, top + row);
	group.value = Rectf();
	group.valueText = entryText(entry);
	group.afterRestart = entry.afterRestart;

	float y = top + row + metrics.gap;

	switch(entry.kind) {

		case Kind::Choice: {
			float x = area.left;
			for(size_t i = 0; i < entry.choices.size(); i++) {
				const float width = metrics.buttonWidth(entry.choices[i]);
				if(x > area.left && x + width > area.right) {
					x = area.left;
					y += row + metrics.gap;
				}
				Button button;
				button.kind = ButtonKind::Choice;
				button.group = entry.group;
				button.index = i;
				button.label = entry.choices[i];
				button.target = "devpanel." + entry.group + "." + std::to_string(i);
				button.rect = Rectf(x, y, x + width, y + row);
				button.current = (i == entry.current);
				buttons.push_back(std::move(button));
				x += width + metrics.gap;
			}
			break;
		}

		case Kind::Number: {
			// "-  value  +": the value as wide as its widest text over the range, so the row does not jump
			const float widest = std::max(metrics.textWidth(numberText(entry.min, entry.unit)),
			                              metrics.textWidth(numberText(entry.max, entry.unit)));
			const float room = std::max(area.width() - 2.f * row - 2.f * metrics.gap, 1.f);
			const float valueWidth = std::min(std::max(widest + 2.f * metrics.padding, 2.f * row), room);
			const float x = area.left;
			Button minus;
			minus.kind = ButtonKind::Minus;
			minus.group = entry.group;
			minus.label = "-";
			minus.target = "devpanel." + entry.group + ".minus";
			minus.rect = Rectf(x, y, x + row, y + row);
			group.value = Rectf(minus.rect.right + metrics.gap, y, minus.rect.right + metrics.gap + valueWidth, y + row);
			Button plus;
			plus.kind = ButtonKind::Plus;
			plus.group = entry.group;
			plus.label = "+";
			plus.target = "devpanel." + entry.group + ".plus";
			plus.rect = Rectf(group.value.right + metrics.gap, y, group.value.right + metrics.gap + row, y + row);
			buttons.push_back(std::move(minus));
			buttons.push_back(std::move(plus));
			break;
		}

	}

	return y + row;
}

//! The height of a group laid out at the top of the area
float groupHeight(const Entry & entry, const Metrics & metrics) {
	GroupLayout group;
	std::vector<Button> buttons;
	return placeGroup(entry, metrics, metrics.area.top, 0, group, buttons) - metrics.area.top;
}

//! The groups in panel order: the PRs in the order of their first group, a PR's groups in registration order
std::vector<const Entry *> panelOrder() {
	std::vector<std::string> prs;
	for(const Item & item : g_items) {
		if(std::find(prs.begin(), prs.end(), item.entry.prLabel) == prs.end()) {
			prs.push_back(item.entry.prLabel);
		}
	}
	std::vector<const Entry *> order;
	for(const std::string & pr : prs) {
		for(const Item & item : g_items) {
			if(item.entry.prLabel == pr) {
				order.push_back(&item.entry);
			}
		}
	}
	return order;
}

//! The groups of each page for a page \a height high: a group never split, a PR's title above its groups
std::vector<std::vector<const Entry *>> paginate(const std::vector<const Entry *> & order, const Metrics & metrics,
                                                 float height) {
	std::vector<std::vector<const Entry *>> pages(1);
	float y = 0.f;
	for(const Entry * entry : order) {
		const bool newBlock = pages.back().empty() || pages.back().back()->prLabel != entry->prLabel;
		const float title = newBlock ? metrics.row + metrics.gap : 0.f;
		const float needed = groupHeight(*entry, metrics);
		if(!pages.back().empty() && y + title + needed > height) {
			pages.emplace_back();
			y = 0.f;
			y += metrics.row + metrics.gap; // the PR's title again on the new page
		} else {
			y += title;
		}
		pages.back().push_back(entry);
		y += needed + metrics.gap;
	}
	return pages;
}

} // anonymous namespace

Layout layout(const Rectf & area, float rowHeight, size_t page, const TextWidth & textWidth) {

	Metrics metrics;
	metrics.area = area;
	metrics.row = rowHeight;
	metrics.gap = rowHeight * 0.25f;
	metrics.padding = rowHeight * 0.3f;
	metrics.textWidth = textWidth ? textWidth : [rowHeight](std::string_view text) {
		return float(text.size()) * rowHeight * 0.5f;
	};

	Layout result;

	if(g_items.empty()) {
		result.empty = true;
		result.notice = "No variants under test";
		Button showMap;
		showMap.kind = ButtonKind::ShowMap;
		showMap.label = "Show map";
		showMap.target = "devpanel.show_map";
		const float top = area.top + rowHeight + metrics.gap; // under the notice
		showMap.rect = Rectf(area.left, top, area.left + metrics.buttonWidth(showMap.label), top + rowHeight);
		result.buttons.push_back(std::move(showMap));
		return result;
	}
	result.empty = false;

	const std::vector<const Entry *> order = panelOrder();
	std::vector<std::vector<const Entry *>> pages = paginate(order, metrics, area.height());
	const bool paged = pages.size() > 1;
	if(paged) {
		// The bottom row holds the page buttons
		pages = paginate(order, metrics, area.height() - rowHeight - metrics.gap);
	}
	result.pages = pages.size();
	result.page = std::min(page, result.pages - 1);

	float y = area.top;
	for(const Entry * entry : pages[result.page]) {
		if(result.blocks.empty() || result.blocks.back().prLabel != entry->prLabel) {
			BlockLayout block;
			block.prLabel = entry->prLabel;
			block.title = Rectf(area.left, y, area.right, y + rowHeight);
			result.blocks.push_back(std::move(block));
			y += rowHeight + metrics.gap;
		}
		GroupLayout group;
		y = placeGroup(*entry, metrics, y, result.blocks.size() - 1, group, result.buttons) + metrics.gap;
		result.groups.push_back(std::move(group));
	}

	if(paged) {
		const float top = area.bottom - rowHeight;
		const float width = std::min(2.f * rowHeight, (area.width() - metrics.gap) / 2.f);
		if(result.page > 0) {
			Button prev;
			prev.kind = ButtonKind::PrevPage;
			prev.label = "<";
			prev.target = "devpanel.prev";
			prev.rect = Rectf(area.left, top, area.left + width, area.bottom);
			result.buttons.push_back(std::move(prev));
		}
		if(result.page + 1 < result.pages) {
			Button next;
			next.kind = ButtonKind::NextPage;
			next.label = ">";
			next.target = "devpanel.next";
			next.rect = Rectf(area.right - width, top, area.right, area.bottom);
			result.buttons.push_back(std::move(next));
		}
	}

	return result;
}

} // namespace devvariants
