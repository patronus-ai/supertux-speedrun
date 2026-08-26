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

/** Mirrors BonusType (src/supertux/player_status.hpp:41-47). Declared here
    rather than included, because src/trace/ is pure data; the mapping and its
    static_asserts live in src/supertux/, as trace_control_map.hpp does for
    Control. */
enum class Bonus : uint8_t
{
  NONE = 0,
  GROWUP = 1,
  FIRE = 2,
  ICE = 3,
  AIR = 4,
  EARTH = 5
};

/** The state a GameSession carries across restart_level(), which therefore has
    to be restored before an episode's step 0 can be reproduced.

    Everything else the level holds is rebuilt from the level file by
    restart_level()'s LevelParser call, so it is not here. What is here is what
    survives that rebuild. See the spec's `start` chunk section for the
    enumeration argument and for the two things deliberately NOT captured
    (Level.data, and any session with more than one player).

    Serialization order is the declaration order below. */
struct StartState final
{
  /** The resolved spawn sector's name. Never empty in a valid record. */
  std::string sector;

  /** The named spawnpoint, or empty when the point is a raw position. The
      engine's own rule (game_session.hpp:63): if a spawnpoint is set, the
      position shall not be, and vice versa. */
  std::string spawnpoint;

  float position_x = 0.0f;
  float position_y = 0.0f;

  /** True exactly when this start came from an activated checkpoint. */
  bool is_checkpoint = false;

  /** GameSession::m_play_time. Reset to 0 only on the start-position branch of
      restart_level() (game_session.cpp:479) and therefore preserved across a
      checkpoint respawn; level timers read it, so it is a gameplay input, not
      a statistic. */
  float play_time = 0.0f;

  uint32_t coins = 0;
  uint32_t tuxdolls = 0;
  Bonus bonus = Bonus::NONE;
  Bonus item_pocket = Bonus::NONE;

  /** GameSession::m_coins_at_start / m_boni_at_start / m_pockets_at_start --
      the accounting reset_level() (game_session.cpp:315-316,326-328) and
      checkpoint coin handling read. restart_level() re-snapshots them from
      the live savegame (game_session.cpp:387-389) rather than restoring them,
      so they are not derivable from the level file. */
  uint32_t coins_at_start = 0;
  Bonus bonus_at_start = Bonus::NONE;
  Bonus pocket_at_start = Bonus::NONE;

  std::vector<uint8_t> encode() const;
  static StartState decode(ByteReader& reader);
};

} // namespace trace

/* EOF */
