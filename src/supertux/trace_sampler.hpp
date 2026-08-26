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

#include <string>

#include "math/vector.hpp"
#include "supertux/direction.hpp"
#include "trace/trace_action.hpp"
#include "trace/trace_visual.hpp"

class Controller;

namespace trace {

/** Reads the gameplay controls out of a Controller into a trace action mask.

    Only the eleven controls in CONTROL_MAP are sampled; menu, escape, cheat,
    debug and console controls are deliberately excluded, because they are not
    gameplay and must not be replayable. The result never sets a reserved bit. */
ActionMask sample_actions(const Controller& controller);

/** Writes a trace action mask into a Controller -- the inverse of
    sample_actions(), and the executor's whole injection path.

    Assigns rather than sets: every mapped control is written on every call,
    so a control the mask omits is released. A replay writes a fresh mask
    into a controller that still holds the previous step's, and an
    implementation that only ever set bits would leave a button held for the
    rest of the episode.

    Only the eleven controls in CONTROL_MAP are touched. Menu, escape,
    cheat, debug and console controls are left exactly as they are, for the
    same reason sample_actions() does not read them: they are not gameplay
    and must not be replayable. A trace cannot open a menu.

    Throws ParseError if the mask has a reserved bit set. */
void apply_actions(Controller& controller, ActionMask mask);

class StringTable;

/** The player state one logical step's visual frame is built from.

    A plain struct rather than a Player reference: linking player.cpp into a
    unit test would drag in most of the engine, and the mapping below is worth
    testing in isolation. Populated field-by-field at the call site
    (GameSession::sample_trace_visual, game_session.cpp) rather than through a
    constructor. */
struct PlayerSnapshot final
{
  Vector pos = Vector(0.0f, 0.0f);
  Vector velocity = Vector(0.0f, 0.0f);
  std::string action;
  uint8_t frame_idx = 0;
  float frame_progress = 0.0f;
  Direction dir = Direction::RIGHT;
  bool visible = false;
  bool active = false;
  bool dead = false;
};

/** Interns the snapshot's action name into `actions` and returns the frame. */
VisualFrame to_visual_frame(const PlayerSnapshot& snapshot, StringTable& actions);

} // namespace trace

/* EOF */
