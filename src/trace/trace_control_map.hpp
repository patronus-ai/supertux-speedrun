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

#include "control/controller.hpp"
#include "trace/trace_action.hpp"

namespace trace {

struct ControlPair final
{
  Action action;
  Control control;
  /** False when THIS engine has no Control for the action. The row still
      exists, because CONTROL_MAP is indexed by action value and
      control_map_is_ordered() requires it stay dense -- an absent action is
      marked, never deleted. Absent rows are skipped by the sampler and by
      the injectivity and range checks. */
  bool present = true;
};

/** The single point of coupling between the trace format and the engine's
    input enum. Menu, escape, cheat, debug and console controls are
    deliberately absent: they are not gameplay and must not be replayable. */
inline constexpr std::array<ControlPair, static_cast<size_t>(Action::COUNT)>
CONTROL_MAP = {{
  { Action::LEFT,       Control::LEFT },
  { Action::RIGHT,      Control::RIGHT },
  { Action::UP,         Control::UP },
  { Action::DOWN,       Control::DOWN },
  { Action::JUMP,       Control::JUMP },
  { Action::ACTION,     Control::ACTION },
  // ITEM has no Control on this engine. 0.6.3's Control enum goes
  // ACTION, START, ... with no ITEM (control/controller.hpp), so bit 6 of
  // the format is simply never set by a trace recorded here -- and a trace
  // from an engine that HAS item still parses, because the bit's meaning is
  // fixed on disk and does not shift.
  { Action::ITEM,       Control::CONTROLCOUNT, false },
  { Action::PEEK_LEFT,  Control::PEEK_LEFT },
  { Action::PEEK_RIGHT, Control::PEEK_RIGHT },
  { Action::PEEK_UP,    Control::PEEK_UP },
  { Action::PEEK_DOWN,  Control::PEEK_DOWN }
}};

namespace detail {

constexpr bool
control_map_is_ordered()
{
  for (size_t i = 0; i < CONTROL_MAP.size(); ++i)
  {
    if (static_cast<size_t>(CONTROL_MAP[i].action) != i)
      return false;
  }
  return true;
}

constexpr bool
control_map_is_injective()
{
  for (size_t i = 0; i < CONTROL_MAP.size(); ++i)
  {
    if (!CONTROL_MAP[i].present) continue;
    for (size_t j = i + 1; j < CONTROL_MAP.size(); ++j)
    {
      if (!CONTROL_MAP[j].present) continue;
      if (CONTROL_MAP[i].control == CONTROL_MAP[j].control)
        return false;
    }
  }
  return true;
}

constexpr bool
control_map_is_in_range()
{
  for (size_t i = 0; i < CONTROL_MAP.size(); ++i)
  {
    if (!CONTROL_MAP[i].present) continue;
    if (static_cast<int>(CONTROL_MAP[i].control) >=
        static_cast<int>(Control::CONTROLCOUNT))
      return false;
  }
  return true;
}

} // namespace detail

static_assert(detail::control_map_is_ordered(),
              "CONTROL_MAP entries must be in Action declaration order");
static_assert(detail::control_map_is_injective(),
              "CONTROL_MAP must not map two actions to the same Control");
static_assert(detail::control_map_is_in_range(),
              "CONTROL_MAP contains a Control outside CONTROLCOUNT");

/** The assertions above are all properties of CONTROL_MAP; none of them says
    anything about the engine's enum. If upstream adds a gameplay Control
    before CONTROLCOUNT, they all still hold and the new control is silently
    absent from every trace. This one fails the build instead, which is what
    the design promised. Bump the literal only after auditing CONTROL_MAP for
    the added control: a gameplay control belongs in the map (and needs a new
    Action bit, taken from the reserved range), a menu/debug control does not
    and only the count changes. */
static_assert(static_cast<int>(Control::CONTROLCOUNT) == 19,
              "Control enum changed: audit CONTROL_MAP for a new gameplay control");
// 19, not the 20 of the fork this came from: that engine has an ITEM
// control and this one does not. See the ITEM row above.

} // namespace trace

/* EOF */
