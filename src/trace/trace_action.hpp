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

namespace trace {

/** Stable serialized bit order for gameplay actions.

    This order is part of the file format and must never be reordered or
    renumbered; append only, and only into the reserved range. It is
    deliberately independent of the engine's Control enum, whose declaration
    order upstream is free to change. */
enum class Action : uint16_t
{
  LEFT = 0,
  RIGHT = 1,
  UP = 2,
  DOWN = 3,
  JUMP = 4,
  ACTION = 5,
  ITEM = 6,
  PEEK_LEFT = 7,
  PEEK_RIGHT = 8,
  PEEK_UP = 9,
  PEEK_DOWN = 10,

  COUNT = 11
};

/** One bit per Action; bits 11-15 are reserved and must be zero. */
using ActionMask = uint16_t;

constexpr ActionMask RESERVED_MASK = 0xF800u;

inline constexpr ActionMask
bit(Action a)
{
  return static_cast<ActionMask>(1u << static_cast<uint16_t>(a));
}

/** The wire name of an action -- "LEFT", "JUMP", ... Shared with
    supertux_trace.py and the comparator's mask rendering, so these strings
    are part of the contract rather than a debug convenience.

    Header-only: this file has no .cpp and TraceActionTest links none, so a
    definition here (rather than a declaration plus a trace_action.cpp) is
    what keeps that true. */
inline constexpr const char*
action_name(Action a)
{
  switch (a)
  {
    case Action::LEFT: return "LEFT";
    case Action::RIGHT: return "RIGHT";
    case Action::UP: return "UP";
    case Action::DOWN: return "DOWN";
    case Action::JUMP: return "JUMP";
    case Action::ACTION: return "ACTION";
    case Action::ITEM: return "ITEM";
    case Action::PEEK_LEFT: return "PEEK_LEFT";
    case Action::PEEK_RIGHT: return "PEEK_RIGHT";
    case Action::PEEK_UP: return "PEEK_UP";
    case Action::PEEK_DOWN: return "PEEK_DOWN";
    case Action::COUNT: break; // not a real action; falls through to the guard
  }
  return "?"; // guards a value outside the declared range -- reachable if this
              // is ever handed a mask decoded from a corrupt or forward-
              // incompatible file rather than one produced by this library
}

} // namespace trace

/* EOF */
