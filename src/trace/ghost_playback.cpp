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

#include "trace/ghost_playback.hpp"

#include <utility>

#include "trace/trace_error.hpp"

namespace trace {

bool
sprite_fields_frozen(const Trace& trace)
{
  if (!trace.has_visual) return false;

  const uint32_t steps = trace.visual.step_count();
  if (steps < FROZEN_SPRITE_MIN_STEPS) return false;

  const VisualFrame& first = trace.visual.at(0);
  for (uint32_t i = 1; i < steps; ++i)
  {
    const VisualFrame& f = trace.visual.at(i);
    // Any one field moving is enough to say the sprite track carries
    // information; the freeze this detects held all three still at once.
    if (f.action_id != first.action_id) return false;
    if (f.frame_idx != first.frame_idx) return false;
    if (f.frame_progress != first.frame_progress) return false;
  }
  return true;
}

GhostPlayback::GhostPlayback(Trace trace) :
  m_trace(std::move(trace)),
  m_empty()
{
  if (!m_trace.has_visual || m_trace.visual.step_count() == 0)
  {
    throw ParseError(
      "this trace carries no visual track, so there is nothing to draw as a "
      "ghost: the visual track is the derived cache a ghost renders from. "
      "Record it with --record-trace, which materializes one, or replay it "
      "with --execute-trace IN --record-trace OUT");
  }
  if (sprite_fields_frozen(m_trace))
  {
    throw ParseError(
      "this trace's sprite fields (action, frame index and frame progress) "
      "are constant for its whole length -- the signature of a --steps "
      "recording made before commit 49f561dd7, when all three were derived "
      "from the draw pass that --steps skips. A ghost drawn from it would "
      "hold a single frozen pose, which looks like a rendering bug and is "
      "not one: the defect is in the trace. Re-record it with this build");
  }
}

bool
GhostPlayback::finished(uint32_t step) const
{
  return step + 1 >= m_trace.visual.step_count();
}

GhostPose
GhostPlayback::pose_at(uint32_t step, float time_offset) const
{
  const uint32_t last = m_trace.visual.step_count() - 1;
  const VisualFrame& f = m_trace.visual.at(step > last ? last : step);

  GhostPose pose;
  // Sub-step interpolation, the same treatment the camera already uses
  // (Camera::get_predicted_transform). time_offset is nonzero only when
  // frame prediction is on (g_config->frame_prediction), so this is the
  // identity in the default configuration -- which is exactly why it must
  // not be assumed either way: a ghost that interpolated unconditionally
  // against a fabricated offset would look correct in one configuration and
  // wrong in the other. The caller passes the context's own offset.
  pose.pos_x = f.pos_x + f.vel_x * time_offset;
  pose.pos_y = f.pos_y + f.vel_y * time_offset;
  pose.vel_x = f.vel_x;
  pose.vel_y = f.vel_y;
  pose.action_id = f.action_id;
  pose.frame_idx = f.frame_idx;
  pose.frame_progress = dequantize_progress(f.frame_progress);
  pose.flags = f.flags;
  return pose;
}

const std::string&
GhostPlayback::action_name(const GhostPose& pose) const
{
  // Range-checked rather than left to StringTable::get()'s throw: this is
  // called once per ghost per draw, and a malformed id is a reason to draw
  // nothing, not to take down the frame. validate() already rejects an
  // out-of-range action_id at read time, so this is defence in depth.
  if (pose.action_id >= m_trace.actions.size()) return m_empty;
  return m_trace.actions.get(pose.action_id);
}

const std::string&
GhostPlayback::sector_at(uint32_t step) const
{
  // sector_transitions is strictly increasing by first_step (an invariant
  // validate() enforces), so the last transition at or before step is the
  // one in force. A linear scan is right here: k is the number of sector
  // changes in a run, which is small, and this is called once per ghost per
  // step.
  uint16_t sector_id = 0;
  bool found = false;
  for (const SectorTransition& t : m_trace.sector_transitions)
  {
    if (t.first_step > step) break;
    sector_id = t.sector_id;
    found = true;
  }
  if (!found) return m_empty;
  if (sector_id >= m_trace.sectors.size()) return m_empty;
  return m_trace.sectors.get(sector_id);
}

std::optional<uint32_t>
GhostPlayback::first_step_crossing_x(float x, const std::string& sector) const
{
  const uint32_t steps = m_trace.visual.step_count();
  if (steps == 0) return std::nullopt;

  // The direction is taken from the ghost's first step IN THIS SECTOR, not
  // from the track's global first position. Sector-local coordinates make
  // that distinction real: a ghost that enters a cave at x = 400 and walks
  // right has never been at x = 75 there, but measured against a track that
  // began at x = 0 in another sector, "75 is to the right of the start"
  // holds and the very first recorded cave step satisfies pos_x >= 75. The
  // marker would then report a crossing that never happened.
  // GhostPlaybackTest.CrossingIgnoresStepsRecordedInAnotherSector found
  // exactly that.
  bool have_origin = false;
  bool rightward = true;
  for (uint32_t i = 0; i < steps; ++i)
  {
    // Sector-local positions again: a step recorded in another sector says
    // nothing about where the ghost was in this one, and comparing its
    // pos_x against the live player's would be comparing two different
    // coordinate spaces.
    if (sector_at(i) != sector) continue;

    const float pos = m_trace.visual.at(i).pos_x;
    if (!have_origin)
    {
      rightward = x >= pos;
      have_origin = true;
    }
    if (rightward ? (pos >= x) : (pos <= x)) return i;
  }
  return std::nullopt;
}

} // namespace trace

/* EOF */
