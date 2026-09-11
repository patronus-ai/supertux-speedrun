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

#include "trace/trace_action.hpp"
#include "trace/trace_file.hpp"

namespace trace {

/** "RIGHT|JUMP" for a mask, "-" for zero. Reserved bits render as
    "reserved:0x…" rather than being dropped: a mask that should never have
    reached a valid trace must be visible if it does. */
std::string describe_mask(ActionMask mask);

enum class DiffKind : uint8_t
{
  HEADER, PRESENCE, INPUTS, VISUAL, START, TRANSITIONS
};

/** The canonical wire name of a kind -- "header", "presence", ... These
    strings are shared with supertux_trace.py and with the committed pair
    corpus, so they are part of the contract, not a debug convenience. */
const char* kind_name(DiffKind kind);

struct Difference final
{
  DiffKind kind = DiffKind::HEADER;
  std::string field;      ///< dotted name, e.g. "seed" or "visual.pos_y"
  bool has_step = false;
  /** A logical step index for INPUTS/VISUAL; a transition index into
      sector_transitions (not a step) for TRANSITIONS. describe() renders the
      two differently -- "step N" vs "transition N" -- because the corpus and
      a human both need to tell which kind of position this number is
      without cross-referencing kind. */
  uint32_t step = 0;
  std::string expected;   ///< rendered; for humans, never compared across languages
  std::string actual;

  std::string describe() const;
};

/** Compares two decoded traces under the spec's three-tier contract.
    Empty result means every tier-1 field is equal. */
std::vector<Difference> compare(const Trace& expected, const Trace& actual);

} // namespace trace

/* EOF */
