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
#include <optional>
#include <string>

#include "trace/trace_file.hpp"

namespace trace {

/** This build's logical step rate, duplicated here as a constant rather than
    included from src/supertux/constants.hpp on purpose: Layer 1 has no engine
    dependencies (CLAUDE.md), and this value is not used to *drive* anything —
    a ghost advances exactly one step per update() call, because a trace is
    step-indexed and never wall-clock-indexed. It exists only to turn a step
    delta into seconds for the marker's label. The authoritative per-trace
    value is header.step_rate, which the executor already refuses on a
    mismatch against LOGICAL_FPS. */
constexpr float LOGICAL_STEP_RATE_HINT = 1000.0f / 15.0f;

/** Below this many steps a constant sprite track is plausible play rather
    than the pre-49f561dd7 freeze, and sprite_fields_frozen() stands down.
    Under a second of play at the step rate above. */
constexpr uint32_t FROZEN_SPRITE_MIN_STEPS = 60;

/** One ghost's drawable state at one instant. pos_x/pos_y are already
    interpolated for the sub-step remainder; every other field is the recorded
    step's own, unmodified. frame_progress is dequantized back to [0, 1). */
struct GhostPose final
{
  float pos_x = 0.0f;
  float pos_y = 0.0f;
  float vel_x = 0.0f;
  float vel_y = 0.0f;
  uint16_t action_id = 0;
  uint8_t frame_idx = 0;
  float frame_progress = 0.0f;
  uint8_t flags = 0;
};

/** True when action_id, frame_idx AND frame_progress are each constant across
    a visual track of at least FROZEN_SPRITE_MIN_STEPS steps.

    That is the signature of a --steps trace recorded before 49f561dd7, when
    all three fields were read off state that only Player::draw() advanced and
    run_steps skips the draw pass — such a trace records one pose for its
    entire length, and a ghost drawn from it looks like a rendering bug when
    the defect is in the input. See the spec's "The visual track's sprite
    fields were draw-derived".

    All three fields, deliberately, rather than the looser "actions table of
    length 1" the P4 item list proposed: an idling Tux legitimately holds one
    action for hundreds of steps, but frame_progress advances every step
    regardless, so keying on the action alone would refuse a legitimate
    recording. Demonstrated by GhostPlaybackTest.AnIdlingTraceIsNotFrozen,
    which fails against the action-only check.

    False for a trace with no visual track: that is a different refusal, made
    by GhostPlayback's constructor. */
bool sprite_fields_frozen(const Trace& trace);

/** Read-only playback over a materialized visual track.

    Pure data, and that is the point of it being here rather than in
    src/object/ beside GhostObject: seeking, interpolating and resolving
    interned names are decisions about a trace, not about the engine, so they
    are unit-testable without instantiating a Sector, a Sprite or a
    DrawingContext. Layer 3 keeps only what genuinely needs engine types. */
class GhostPlayback final
{
public:
  /** Throws ParseError when the trace carries no visual track (a ghost has
      nothing to draw) or when sprite_fields_frozen() holds. Both refusals
      are made here rather than at the CLI so that any caller that acquires a
      trace — not only --ghost — inherits them. */
  explicit GhostPlayback(Trace trace);

  inline uint32_t step_count() const { return m_trace.visual.step_count(); }

  /** True once step is at or past the last recorded one. A ghost outliving
      its trace is ordinary — the live player is still playing — so this is a
      question to ask, not an error. */
  bool finished(uint32_t step) const;

  /** The pose at step, with position advanced by the recorded velocity for
      time_offset seconds. Clamps to the final frame past the end rather than
      throwing, so a ghost holds its last pose instead of vanishing. */
  GhostPose pose_at(uint32_t step, float time_offset) const;

  /** The interned action name for a pose, or "" when the id is out of range.
      The range check is defensive: validate() already rejects an out-of-range
      action_id at read time, and StringTable::get() would throw. */
  const std::string& action_name(const GhostPose& pose) const;

  /** The name of the sector the recorded player was in at step, resolved
      through the sector_transitions table (positions in the visual track are
      sector-local, so this is what makes a position mean anything). */
  const std::string& sector_at(uint32_t step) const;

  /** The earliest step at which the recorded player's pos_x crossed x while
      in `sector`, or nullopt when it never did.

      This is what the delta marker's signed time is measured against: the
      ghost is at step N and the live player is at x now, so if the ghost
      crossed x at step S, the ghost is (N - S) steps ahead. Positive means
      the ghost got there first.

      "Crossed" rather than "equalled", because positions are continuous and
      a recorded step almost never lands exactly on the live player's x. The
      direction is taken from the ghost's first step IN THIS SECTOR rather
      than assumed rightward: if x is to the right of where the ghost
      started there, the first step with pos_x >= x is the crossing; if it
      is to the left, the first with pos_x <= x. A ghost that walks left is
      as ordinary as one that walks right, and a rightward-only test would
      silently return nullopt for its whole run. Taking the origin from the
      *sector's* first step rather than the track's is what stops a cave
      entered at x = 400 from reporting a crossing at x = 75 that never
      happened. */
  std::optional<uint32_t> first_step_crossing_x(float x,
                                                const std::string& sector) const;

  inline const Trace& trace() const { return m_trace; }

private:
  Trace m_trace;
  /** Returned by reference from the two lookups above when an id is out of
      range; a member so the reference outlives the call. */
  std::string m_empty;

private:
  GhostPlayback(const GhostPlayback&) = delete;
  GhostPlayback& operator=(const GhostPlayback&) = delete;
};

} // namespace trace

/* EOF */
