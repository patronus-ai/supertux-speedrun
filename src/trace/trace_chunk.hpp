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

#include <cstdint>
#include <string>
#include <vector>

namespace trace {

class ByteReader;
class ByteWriter;

/** One length-prefixed, named blob.

    Wire layout: NUL-terminated ASCII name, uint32 payload length, payload.
    A reader that does not recognise a name keeps the chunk and ignores it,
    which is the format's forward-compatibility mechanism. */
struct Chunk final
{
  std::string name;
  std::vector<uint8_t> payload;
};

void write_chunk(ByteWriter& writer, const std::string& name,
                 const std::vector<uint8_t>& payload);

/** Reads chunks until the reader is exhausted. Throws ParseError on a
    truncated chunk, a non-ASCII name, or a duplicate name. */
std::vector<Chunk> read_chunks(ByteReader& reader);

/** Returns nullptr when no chunk carries that name. */
const Chunk* find_chunk(const std::vector<Chunk>& chunks, const std::string& name);

} // namespace trace

/* EOF */
