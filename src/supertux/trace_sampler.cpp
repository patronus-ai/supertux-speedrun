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

#include "supertux/trace_sampler.hpp"

#include "control/controller.hpp"
#include "trace/trace_control_map.hpp"
#include "trace/trace_error.hpp"
#include "trace/trace_strings.hpp"

namespace trace {

ActionMask
sample_actions(const Controller& controller)
{
  ActionMask mask = 0;
  for (const ControlPair& pair : CONTROL_MAP)
  {
    // Skip an action this engine has no control for (see ControlPair).
    if (!pair.present) continue;
    if (controller.hold(pair.control))
      mask |= bit(pair.action);
  }

  return mask;
}

void
apply_actions(Controller& controller, ActionMask mask)
{
  if ((mask & RESERVED_MASK) != 0)
    throw ParseError("action mask has a reserved bit set");

  for (const ControlPair& pair : CONTROL_MAP)
  {
    // Skip an action this engine has no control for (see ControlPair).
    if (!pair.present) continue;
    controller.set_control(pair.control, (mask & bit(pair.action)) != 0);
  }
}

VisualFrame
to_visual_frame(const PlayerSnapshot& snapshot, StringTable& actions)
{
  VisualFrame frame;
  frame.pos_x = snapshot.pos.x;
  frame.pos_y = snapshot.pos.y;
  frame.vel_x = snapshot.velocity.x;
  frame.vel_y = snapshot.velocity.y;
  frame.action_id = actions.intern(snapshot.action);
  frame.frame_idx = snapshot.frame_idx;
  frame.frame_progress = quantize_progress(snapshot.frame_progress);

  uint8_t flags = 0;
  if (snapshot.dir == Direction::RIGHT)
    flags |= VisualFlags::FACING_RIGHT;
  if (snapshot.visible)
    flags |= VisualFlags::VISIBLE;
  if (snapshot.active)
    flags |= VisualFlags::ACTIVE;
  if (snapshot.dead)
    flags |= VisualFlags::DEAD;

  frame.flags = flags;
  return frame;
}

} // namespace trace

/* EOF */
