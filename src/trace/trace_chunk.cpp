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

#include "trace/trace_chunk.hpp"

#include <set>

#include "trace/trace_bytes.hpp"
#include "trace/trace_error.hpp"

namespace trace {

namespace {

/** The format specifies chunk names as ASCII. "ASCII" here means every byte
    is <= 0x7F, which is exactly what the Python reference's str.isascii()
    accepts after its UTF-8 decode, so both implementations reject the same
    names. Enforced on read and on write: a name our writer would never
    produce must still not be accepted from a third party, because the two
    implementations must agree on what is *valid*, not merely on what
    parses. */
bool is_ascii(const std::string& name)
{
  for (const char c : name)
  {
    if ((static_cast<unsigned char>(c) & 0x80u) != 0)
      return false;
  }
  return true;
}

} // namespace

void
write_chunk(ByteWriter& writer, const std::string& name,
            const std::vector<uint8_t>& payload)
{
  if (name.empty())
    throw ParseError("chunk name must not be empty");

  if (!is_ascii(name))
    throw ParseError("chunk name must be ASCII");

  writer.str(name);
  writer.u32(static_cast<uint32_t>(payload.size()));
  if (!payload.empty())
    writer.bytes(payload.data(), payload.size());
}

std::vector<Chunk>
read_chunks(ByteReader& reader)
{
  std::vector<Chunk> out;

  // Duplicate names are rejected rather than resolved. find_chunk() below
  // returns the first match while the Python reference's dict(chunks) keeps
  // the last, so a file with two "header" chunks would decode to two
  // different traces -- and either way one copy is silently discarded. No
  // format-version bump: such a file was never meaningfully valid.
  std::set<std::string> seen;

  while (!reader.exhausted())
  {
    Chunk chunk;
    chunk.name = reader.str();
    if (chunk.name.empty())
      throw ParseError("chunk name must not be empty");

    if (!is_ascii(chunk.name))
      throw ParseError("chunk name must be ASCII");

    if (!seen.insert(chunk.name).second)
      throw ParseError("duplicate chunk name: " + chunk.name);

    const uint32_t length = reader.u32();
    if (length > reader.remaining())
      throw ParseError("chunk payload is truncated");

    chunk.payload.resize(length);
    if (length > 0)
      reader.bytes(chunk.payload.data(), length);

    out.push_back(std::move(chunk));
  }

  return out;
}

const Chunk*
find_chunk(const std::vector<Chunk>& chunks, const std::string& name)
{
  for (const Chunk& chunk : chunks)
  {
    if (chunk.name == name)
      return &chunk;
  }
  return nullptr;
}

} // namespace trace

/* EOF */
