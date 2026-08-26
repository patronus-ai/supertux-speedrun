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

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "trace/trace_chunk.hpp"
#include "trace/trace_header.hpp"
#include "trace/trace_inputs.hpp"
#include "trace/trace_notes.hpp"
#include "trace/trace_start.hpp"
#include "trace/trace_strings.hpp"
#include "trace/trace_visual.hpp"

namespace trace {

class ByteReader;

inline constexpr std::array<char, 4> MAGIC = { 'S', 'T', 'G', 'T' };
inline constexpr uint16_t FORMAT_VERSION = 2;

/** Marks the step at which the recorded player entered a sector. Positions in
    the visual track are sector-local, so playback needs this to know which
    sector a frame belongs to. */
struct SectorTransition final
{
  uint16_t sector_id = 0;
  uint32_t first_step = 0;
};

std::vector<uint8_t> encode_transitions(
  const std::vector<SectorTransition>& transitions);
std::vector<SectorTransition> decode_transitions(ByteReader& reader);

/** A complete trace.

    inputs is the ground truth; visual is a cache materialized by executing
    inputs. unknown_chunks holds chunks written by a newer build, carried
    through unchanged so a round-trip here never destroys them. */
struct Trace final
{
  Header header;
  InputTrack inputs;

  bool has_visual = false;
  VisualTrack visual;
  StringTable actions;
  StringTable sectors;
  std::vector<SectorTransition> sector_transitions;

  NoteStore notes;

  /** Present exactly when header.start_kind is not UNKNOWN. */
  bool has_start = false;
  StartState start;

  std::vector<Chunk> unknown_chunks;
};

/** Throws ParseError describing the first invariant violated. */
void validate(const Trace& trace);

/** Validates, then writes preamble + deflated chunk stream. */
std::vector<uint8_t> serialize(const Trace& trace);

/** Reads a whole file into bytes, for deserialize(). Lives here rather than
    beside an executor -- the fork this came from keeps it in
    supertux/trace_executor.cpp, which this engine has no equivalent of.
    Throws ParseError naming the file if it cannot be opened or sized. */
std::vector<uint8_t> read_file_bytes(const std::string& path);

/** Reads and validates. Throws ParseError on any malformed input. */
Trace deserialize(const uint8_t* data, size_t size);

/** Reads and validates ONLY the header chunk, skipping decompression of
    everything after it whenever that is possible without changing what is
    returned. serialize() always writes "header" as the very first chunk, so
    a bounded read of the front of the inflated stream finds it without
    inflating the (typically much larger) inputs/visual tracks -- no second
    wire format, and nothing here that supertux_trace.py needs to mirror.

    Falls back to a full deserialize() -- so this always returns exactly
    what deserialize(data, size).header would, never anything different --
    whenever the fast path cannot find a complete header chunk within a
    generous fixed budget: an unusually large header, a corrupt file, or
    (defensively) a file shaped some way this library's own writer never
    produces. Throws ParseError under exactly the conditions deserialize()
    does, because the fallback IS deserialize(). */
Header read_header(const uint8_t* data, size_t size);

} // namespace trace

/* EOF */
