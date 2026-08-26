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
#include <cstdint>
#include <string>

#include "trace/trace_header.hpp"

namespace trace {

/** What the library does with one stored trace on one level. */
enum class StoreVerdict : uint8_t
{
  SKIP = 0,
  REPLAY = 1,
  /** Replays, but the level has changed underneath it: same path,
      different digest. The caller logs and suffixes the ghost's label. */
  REPLAY_STALE = 2,
};

/** The selection rule: does one stored trace's header race on the level
    about to be played?

    header is read, never mutated -- this is a pure function of the three
    arguments. level_path and level_md5 describe the level about to run;
    header carries the same two fields for the level the trace was recorded
    on, plus producer_kind, which is the first thing checked. See
    ghost_store.cpp for why the digest is checked before the path, and
    ghost_store_test.cpp for the case table this satisfies. */
StoreVerdict store_verdict(const Header& header,
                           const std::string& level_path,
                           const std::array<uint8_t, 16>& level_md5);

} // namespace trace

/* EOF */
