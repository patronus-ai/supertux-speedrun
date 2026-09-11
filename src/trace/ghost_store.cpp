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

#include "trace/ghost_store.hpp"

namespace trace {

namespace {

bool
is_all_zero(const std::array<uint8_t, 16>& digest)
{
  for (uint8_t byte : digest)
    if (byte != 0)
      return false;
  return true;
}

} // namespace

StoreVerdict
store_verdict(const Header& header,
              const std::string& level_path,
              const std::array<uint8_t, 16>& level_md5)
{
  // Only agents race. A recorded human run lands in the same directory and
  // is skipped here, which is the whole of "agent traces in, human runs
  // out" -- see the design's "The rule, stated exactly".
  if (header.producer_kind != ProducerKind::AGENT)
    return StoreVerdict::SKIP;

  // Digest first: it survives a machine boundary and an absolute path does
  // not. An all-zero digest is "unknown" (CLAUDE.md's standing rule), so it
  // must never satisfy this branch -- not even against another all-zero
  // level_md5, which is why the guard checks the incoming level_md5 too and
  // not just header.level_md5.
  if (!is_all_zero(level_md5) && header.level_md5 == level_md5)
    return StoreVerdict::REPLAY;

  // No digest match: the level has moved or been edited. A matching path is
  // still the same level file, just changed underneath the trace, so it
  // replays but is flagged stale rather than treated as a clean hit.
  if (!header.level_path.empty() && header.level_path == level_path)
    return StoreVerdict::REPLAY_STALE;

  return StoreVerdict::SKIP;
}

} // namespace trace

/* EOF */
