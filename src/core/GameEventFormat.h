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

#ifndef ARX_CORE_GAMEEVENTFORMAT_H
#define ARX_CORE_GAMEEVENTFORMAT_H

#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>

/*!
 * \brief The text of one game event line (#130) - pure, no engine state, covered by GameEventTest
 *
 * An event is \c <name> followed by \c key=value fields:
 *  - integers as they are, floats with a fixed number of decimals (\c nan / \c inf spelled out),
 *  - booleans as \c yes / \c no,
 *  - strings bare when they are a single word, otherwise in double quotes with \c " and \c \\
 *    escaped by a backslash; an empty string is \c "" and line breaks become \c \\n,
 * so a line splits on spaces outside quotes and every value reads back unambiguously.
 */
namespace gameevent {

//! A string value as it appears in a line: bare if it is one plain word, else quoted and escaped
inline std::string quoteValue(std::string_view value) {

	bool plain = !value.empty();
	for(char c : value) {
		if(c == ' ' || c == '=' || c == '"' || c == '\\' || static_cast<unsigned char>(c) < 0x20) {
			plain = false;
			break;
		}
	}
	if(plain) {
		return std::string(value);
	}

	std::string result = "\"";
	for(char c : value) {
		if(c == '"' || c == '\\') {
			result += '\\';
			result += c;
		} else if(c == '\n') {
			result += "\\n";
		} else if(c == '\r') {
			result += "\\r";
		} else if(static_cast<unsigned char>(c) < 0x20) {
			result += ' ';
		} else {
			result += c;
		}
	}
	result += '"';
	return result;
}

//! Builds \c "<name> key=value key=value"
class Line {

	std::string m_text;

	void key(std::string_view key) {
		m_text += ' ';
		m_text += key;
		m_text += '=';
	}

public:

	explicit Line(std::string_view name) : m_text(name) { }

	Line & field(std::string_view name, std::string_view value) {
		key(name);
		m_text += quoteValue(value);
		return *this;
	}

	Line & field(std::string_view name, const std::string & value) {
		return field(name, std::string_view(value));
	}

	Line & field(std::string_view name, const char * value) {
		return field(name, std::string_view(value ? value : ""));
	}

	Line & field(std::string_view name, bool value) {
		key(name);
		m_text += value ? "yes" : "no";
		return *this;
	}

	template <typename T, typename = std::enable_if_t<std::is_integral_v<T>>>
	Line & field(std::string_view name, T value) {
		key(name);
		m_text += std::to_string(value);
		return *this;
	}

	//! A float with \a decimals digits after the point
	Line & field(std::string_view name, double value, int decimals = 2) {
		key(name);
		if(std::isnan(value)) {
			m_text += "nan";
		} else if(std::isinf(value)) {
			m_text += value < 0 ? "-inf" : "inf";
		} else {
			std::ostringstream oss;
			oss << std::fixed << std::setprecision(decimals) << value;
			m_text += oss.str();
		}
		return *this;
	}

	Line & field(std::string_view name, float value, int decimals = 2) {
		return field(name, double(value), decimals);
	}

	const std::string & str() const { return m_text; }

};

/*!
 * Counts events by name and keeps a noisy kind from flooding the log (F-105 FIRST_N): the first
 * \c limit events of a name are logged, the next one is replaced by a single notice and the rest
 * are only counted. A limit of 0 means no limit.
 */
class Limiter {

	std::map<std::string, size_t, std::less<>> m_counts;
	size_t m_total = 0;

public:

	enum Verdict {
		Log,     //!< Log the event
		Notice,  //!< Log that further events of this name are not logged, instead of the event
		Drop     //!< Count only
	};

	Verdict count(std::string_view name, size_t limit) {
		m_total++;
		auto it = m_counts.find(name);
		if(it == m_counts.end()) {
			it = m_counts.emplace(std::string(name), 0).first;
		}
		size_t seen = ++it->second;
		if(limit == 0 || seen <= limit) {
			return Log;
		}
		return seen == limit + 1 ? Notice : Drop;
	}

	//! Events seen so far, logged or not
	size_t total() const { return m_total; }

	//! Events seen so far by name, logged or not
	const std::map<std::string, size_t, std::less<>> & counts() const { return m_counts; }

};

} // namespace gameevent

#endif // ARX_CORE_GAMEEVENTFORMAT_H
