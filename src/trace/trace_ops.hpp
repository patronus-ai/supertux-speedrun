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

#include "trace/trace_file.hpp"

namespace trace {

/** Half-open [begin, end). Renumbers notes and sector transitions relative to
    the new origin, synthesizes the sector active at begin as a transition at
    step 0, and marks the outcome INCOMPLETE unless end is the original end.

    Assumes source already satisfies validate()'s invariants, in particular
    that sector_transitions is strictly increasing by first_step. Behavior is
    undefined otherwise; this function does not re-validate its input.

    Throws ParseError when begin > end or end exceeds the source length. */
Trace slice(const Trace& source, uint32_t begin, uint32_t end);

/** slice(source, 0, steps). */
Trace truncate(const Trace& source, uint32_t steps);

/** Joins two traces of the same level, fingerprint, step rate and seed.
    Re-interns the second trace's strings into the first's tables and remaps its
    ids.

    Assumes both inputs already satisfy validate()'s invariants, in particular
    that sector_transitions is strictly increasing by first_step. Behavior is
    undefined otherwise; this function does not re-validate its input.

    Throws ParseError on any header mismatch. */
Trace concat(const Trace& first, const Trace& second);

} // namespace trace

/* EOF */
