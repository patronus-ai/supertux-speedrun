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

#include "trace/trace_strings.hpp"

#include "trace/trace_bytes.hpp"
#include "trace/trace_error.hpp"

namespace trace {

StringTable::StringTable() :
  m_values(),
  m_ids()
{
}

uint16_t
StringTable::intern(const std::string& value)
{
  const std::unordered_map<std::string, uint16_t>::const_iterator it =
    m_ids.find(value);
  if (it != m_ids.end())
    return it->second;

  if (m_values.size() >= MAX_ENTRIES)
    throw ParseError("string table is full");

  const uint16_t id = static_cast<uint16_t>(m_values.size());
  m_values.push_back(value);
  m_ids[value] = id;
  return id;
}

const std::string&
StringTable::get(uint16_t id) const
{
  if (static_cast<size_t>(id) >= m_values.size())
    throw ParseError("string table id out of range");

  return m_values[id];
}

std::vector<uint8_t>
StringTable::encode() const
{
  ByteWriter writer;
  writer.u16(static_cast<uint16_t>(m_values.size()));
  for (const std::string& value : m_values)
    writer.str(value);

  return writer.data();
}

StringTable
StringTable::decode(ByteReader& reader)
{
  const uint16_t count = reader.u16();

  StringTable table;
  for (uint16_t i = 0; i < count; ++i)
  {
    const std::string value = reader.str();
    if (table.m_ids.find(value) != table.m_ids.end())
      throw ParseError("duplicate entry in string table");

    table.intern(value);
  }

  return table;
}

} // namespace trace

/* EOF */
