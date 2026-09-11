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

#include <memory>
#include <string>

#include "math/vector.hpp"
#include "object/ghost_marker.hpp"
#include "sprite/sprite_ptr.hpp"
#include "supertux/game_object.hpp"
#include "trace/ghost_playback.hpp"
#include "video/color.hpp"

/** How one ghost looks and is named. Owned by GhostManager rather than by the
    GhostObject, so k ghosts stay distinguishable (desideratum 16) and a ghost
    keeps its identity across the re-attach that every sector change forces. */
struct GhostStyle final
{
  Color tint = Color(1.0f, 1.0f, 1.0f);
  float alpha = 0.5f;
  std::string label;
};

/** A recorded trace, drawn.

    Inertness is structural, not a review note (desideratum 17), and each of
    the four properties is pinned by a test that was demonstrated to fail
    when the property was removed:

    - is_saveable() is false, so the editor can never write a ghost into a
      level file;
    - it derives from GameObject and NOT from MovingObject, and registers no
      collision, so the collision system never sees it;
    - it draws only: it names neither gameRandom nor graphicsRandom, and
      writes to nothing outside itself;
    - its drawing is gated on a switch that leaves its simulation running
      (desideratum 18) -- update() keeps advancing the step clock while
      rendering is off, so a ghost re-enabled later is still in the right
      place.

    THE STEP CLOCK is exactly one step per update() call, and deliberately
    ignores dt_sec. A trace is step-indexed and never wall-clock-indexed (the
    spec's "Step is the unit"), and ScreenManager calls Screen::update() once
    per logical step, so this keeps a ghost step-synchronous with the live
    player. Accumulating dt here would reintroduce the wall clock the whole
    format exists to exclude. */
class GhostObject final : public GameObject
{
public:
  GhostObject(std::shared_ptr<const trace::GhostPlayback> playback,
              GhostStyle style,
              const bool& render_enabled,
              const std::string& sector_name,
              uint32_t start_step);
  ~GhostObject() override;

  virtual void update(float dt_sec) override;
  virtual void draw(DrawingContext& context) override;

  // is_saveable() is the load-bearing one and 0.6.3 has it: the editor can
  // never write a ghost into a level file. track_state() does not exist on
  // this engine (it is a later addition) and is omitted rather than stubbed;
  // the state-tracking it opts out of has nothing to opt out of here. This
  // engine spells the class hook get_class(), not get_class_name().
  virtual bool is_saveable() const override { return false; }
  virtual std::string get_class() const override { return "ghost"; }
  virtual std::string get_display_name() const override { return "Ghost"; }

  inline uint32_t step() const { return m_step; }

  /** True when the recorded player was in THIS object's sector at m_step.
      A ghost elsewhere draws nothing and is reported by the delta marker as
      "other sector" instead -- positions in a visual track are sector-local,
      so drawing an off-sector ghost's coordinates here would place it at a
      meaningless point in this sector. */
  bool in_this_sector() const;

  /** The ghost's position at the current step, uninterpolated. Valid whether
      or not the ghost is being drawn, because the marker pass needs it even
      when rendering is off. */
  Vector current_pos() const;

  inline const GhostStyle& style() const { return m_style; }

  /** The marker state as of the last update(). Computed in update() rather
      than in draw() for one reason: --steps has no draw pass, so a state
      computed while drawing could not be observed by any deterministic
      test, and the three-state requirement would rest on an argument. It is
      a pure read of the camera, the players and this ghost's own playback --
      it writes nothing outside this object, which is what keeps
      tools/trace/test_ghost_inertness_probe.py's byte-identical pair
      holding with it in place. */
  inline const GhostMarker& marker() const { return m_marker; }
  inline const trace::GhostPlayback& playback() const { return *m_playback; }

private:
  /** shared_ptr because GhostManager owns the playback and the GhostObjects
      are destroyed and rebuilt on every sector change; a trace is also large
      enough that copying it per sector change would be wasteful. */
  std::shared_ptr<const trace::GhostPlayback> m_playback;
  GhostStyle m_style;
  /** A reference to GhostManager's switch, not a copy, so toggling it takes
      effect without reaching into every attached ghost. */
  const bool& m_render_enabled;
  std::string m_sector_name;
  uint32_t m_step;
  SpritePtr m_sprite;
  GhostMarker m_marker;
  /** So the state is logged on CHANGE rather than once per step: a line per
      step would be hundreds of lines a run, and the probe wants transitions
      anyway. */
  GhostMarkerState m_logged_state;
  /** So the FIRST state is logged too. Without it, a ghost that begins (and
      stays) in m_logged_state's initial value is never logged at all, and a
      test looking for that state sees nothing -- measured: HIDDEN went
      unreported for a whole run. */
  bool m_marker_logged;
  /** Logged alongside the state, because a state line alone cannot show
      whether the delta was computable. Tracked so the line is emitted when
      EITHER changes -- a ghost that reaches ARROW while its delta is still
      unknown and never changes state again would otherwise never report a
      delta at all, and the probe would have nothing to assert. */
  bool m_logged_delta_known;

private:
  GhostObject(const GhostObject&) = delete;
  GhostObject& operator=(const GhostObject&) = delete;
};

/* EOF */
