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

#include "object/ghost_manager.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <fmt/format.h>

#include "object/camera.hpp"
#include "supertux/resources.hpp"
#include "supertux/sector.hpp"
#include "trace/trace_file.hpp"
#include "util/file_system.hpp"
#include "util/log.hpp"
#include "video/drawing_context.hpp"
#include "video/layer.hpp"
#include "video/video_system.hpp"

namespace {

/** Distinguishable at reduced alpha and distinct from Tux's own palette
    (desideratum 16, "N simultaneous ghosts, with distinguishable color and
    label"). Wraps past six ghosts rather than refusing: k is the user's
    business, and two ghosts sharing a colour is a legibility problem, not an
    error. */
const Color GHOST_PALETTE[] = {
  Color(0.35f, 0.75f, 1.00f),   // cyan
  Color(1.00f, 0.55f, 0.25f),   // orange
  Color(0.65f, 1.00f, 0.45f),   // green
  Color(1.00f, 0.45f, 0.85f),   // magenta
  Color(1.00f, 0.90f, 0.35f),   // yellow
  Color(0.70f, 0.60f, 1.00f),   // violet
};

constexpr size_t GHOST_PALETTE_SIZE =
  sizeof(GHOST_PALETTE) / sizeof(GHOST_PALETTE[0]);

/** Within this many pixels of a screen edge, a marker's label is aligned
    away from that edge instead of centred on it. */
constexpr float LABEL_EDGE_ZONE = 200.0f;

/** Vertical gap from the marker position to its label, chosen to clear the
    arrow's base rather than overlap it. */
constexpr float ARROW_LABEL_GAP = 26.0f;

}  // namespace

GhostManager::GhostManager() :
  m_playbacks(),
  m_styles(),
  m_render_enabled(true),
  m_attached(),
  m_steps()
{
}

GhostManager::~GhostManager()
{
}

void
GhostManager::load(const std::vector<std::string>& paths)
{
  for (const std::string& path : paths)
  {
    try
    {
      // read_file_bytes and deserialize are the executor's own reading path
      // (src/supertux/trace_executor.cpp), reused rather than reimplemented:
      // a second reader in this engine is exactly the kind of divergence the
      // format's cross-validation discipline exists to prevent.
      const std::vector<uint8_t> bytes = trace::read_file_bytes(path);
      trace::Trace loaded = trace::deserialize(bytes.data(), bytes.size());

      m_playbacks.push_back(
        std::make_shared<const trace::GhostPlayback>(std::move(loaded)));

      GhostStyle style;
      style.tint = GHOST_PALETTE[m_styles.size() % GHOST_PALETTE_SIZE];
      style.alpha = 0.5f;
      style.label = FileSystem::strip_extension(FileSystem::basename(path));
      m_styles.push_back(std::move(style));
    }
    catch (const std::exception& e)
    {
      // Naming the file matters when k ghosts were given and one is bad --
      // "trace parse error" alone would leave the user to guess which.
      throw std::runtime_error("cannot use '" + path + "' as a ghost: " +
                               e.what());
    }
  }
}

void
GhostManager::mark_last_stale()
{
  if (!m_styles.empty())
    m_styles.back().label += " (stale)";
}

void
GhostManager::attach_to_sector(Sector& sector, const std::string& sector_name,
                               bool restart)
{
  // Read each ghost's clock off the outgoing objects BEFORE dropping them.
  // A sector transition destroys nothing here -- the old Sector still owns
  // its GhostObjects -- but it stops updating them, so this is the last
  // chance to learn where they had got to.
  m_steps.resize(m_playbacks.size(), 0);
  if (restart)
  {
    // The player's attempt started over, so the ghosts race it again from
    // the beginning. A ghost that kept counting through a death would be
    // minutes ahead and permanently off screen.
    std::fill(m_steps.begin(), m_steps.end(), 0u);
  }
  else
  {
    for (size_t i = 0; i < m_attached.size() && i < m_steps.size(); ++i)
      m_steps[i] = m_attached[i]->step();
  }

  // The Sector owns the objects, and the ones from before belong to a sector
  // that is no longer being updated. Clearing before adding is not a
  // tidy-up, it is the correctness step.
  m_attached.clear();

  for (size_t i = 0; i < m_playbacks.size(); ++i)
  {
    // m_render_enabled by reference: the ghost reads the switch, it does not
    // own a copy of it, so set_render_enabled() takes effect on every
    // attached ghost at once.
    m_attached.push_back(&sector.add<GhostObject>(
      m_playbacks[i], m_styles[i], m_render_enabled, sector_name,
      m_steps[i]));
  }

  // Logged so that "a ghost was attached" is OBSERVABLE and not merely
  // assumed. The phase's central assertion is that a recording made with a
  // ghost active is byte-identical to one made without -- and an unattached
  // ghost satisfies that vacuously. tools/trace/test_ghost_inertness_probe.py
  // asserts this line appears, so the pair it compares is known to have had a
  // real ghost in it. log_info, so it needs --verbose and stays out of an
  // ordinary run's output.
  log_info << "ghosts: attached " << m_attached.size() << " to sector '"
           << sector_name << "'"
           << (restart ? " (restart, step 0)" : " (transition, step kept)")
           << std::endl;
}

void
GhostManager::draw_markers(DrawingContext& context, Sector& sector)
{
  // Desideratum 18: the whole pass is gated, not each marker. Ghost
  // simulation is untouched -- the GhostObjects inside the sector went on
  // updating and went on advancing their step clocks to produce the states
  // being skipped here.
  if (!m_render_enabled) return;

  // GameSession::draw calls this AFTER Sector::draw has popped its
  // transform, so this canvas is in screen coordinates while a marker's
  // clamped_pos is in world coordinates (Camera::get_rect() is a world
  // rect). Without this the markers would be drawn at the world position,
  // which is off screen by exactly the camera translation -- i.e. invisible
  // on any level whose camera has scrolled at all.
  const Vector translation = sector.get_camera().get_translation();

  for (const GhostObject* ghost : m_attached)
  {
    const GhostMarker& marker = ghost->marker();
    if (marker.state == GhostMarkerState::HIDDEN) continue;

    const Vector at = marker.clamped_pos - translation;
    const Color& tint = ghost->style().tint;

    if (marker.state == GhostMarkerState::ARROW)
    {
      // A triangle pointing along marker.direction: the tip one ARROW_TIP
      // out, the base two corners ARROW_HALF_BASE either side of the
      // perpendicular. draw_triangle takes three points, so the arrow can
      // point in any direction rather than being one of four sprites.
      constexpr float ARROW_TIP = 22.0f;
      constexpr float ARROW_HALF_BASE = 12.0f;
      const Vector dir = marker.direction;
      const Vector perp(-dir.y, dir.x);
      context.color().draw_triangle(
        at + dir * ARROW_TIP,
        at - dir * 2.0f + perp * ARROW_HALF_BASE,
        at - dir * 2.0f - perp * ARROW_HALF_BASE,
        tint, LAYER_HUD);
    }

    // The label, and the signed delta when there is one. Positive means the
    // ghost is ahead -- it reached where the player is now in fewer steps.
    std::string text = ghost->style().label;
    if (marker.state == GhostMarkerState::OTHER_SECTOR)
    {
      text += " (other sector)";
    }
    else if (marker.delta_known)
    {
      text += fmt::format(" {:+.2f}s", marker.delta_seconds);
    }

    // The alignment follows which edge the marker landed on. A fixed margin
    // cannot clear an arbitrarily long ghost name, and a centred label at the
    // left edge loses its first half -- measured in P4's visual pass, where
    // "walk-ahead" rendered as "ahead". Aligning away from the near edge
    // makes the label grow inward instead of off-screen.
    const Rectf screen = context.get_rect();
    FontAlignment align = FontAlignment::ALIGN_CENTER;
    if (at.x < screen.get_left() + LABEL_EDGE_ZONE)
      align = FontAlignment::ALIGN_LEFT;
    else if (at.x > screen.get_right() - LABEL_EDGE_ZONE)
      align = FontAlignment::ALIGN_RIGHT;

    // Below the arrow, and far enough below to clear its base rather than
    // overlapping it.
    context.color().draw_text(Resources::normal_font, text,
                              at + Vector(0.0f, ARROW_LABEL_GAP),
                              align, LAYER_HUD, tint);
  }
}

/* EOF */
