//  SuperTux
//  Copyright (C) 2026 Abdelrahman Madkour <abdelrahman.madkour@patronus.ai>
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace trace {

class ByteReader;

/** Deduplicating string table. Ids are assigned in first-seen order and are
    stable for the lifetime of the table, so a recorded id always refers to the
    same string within one trace.

    Wire layout: uint16 count, then count NUL-terminated strings. */
class StringTable final
{
public:
  /** The count field is a uint16, so ids run 0 .. MAX_ENTRIES-1. */
  static constexpr size_t MAX_ENTRIES = 65535;

  StringTable();

  /** Returns the existing id when value is already present. Throws ParseError
      when the table is full. */
  uint16_t intern(const std::string& value);

  /** Throws ParseError when id is out of range. */
  const std::string& get(uint16_t id) const;

  inline size_t size() const { return m_values.size(); }
  inline bool empty() const { return m_values.empty(); }

  std::vector<uint8_t> encode() const;
  static StringTable decode(ByteReader& reader);

private:
  std::vector<std::string> m_values;
  std::unordered_map<std::string, uint16_t> m_ids;
};

} // namespace trace

/* EOF */
