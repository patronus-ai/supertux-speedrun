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

#include "math/rectf.hpp"
#include "math/vector.hpp"

/** Which of the delta marker's three states a ghost is in.

    The spec's Layer 3 names all three: "an arrow clamped to the viewport edge
    showing direction and signed time delta against the live player, or 'other
    sector' when the ghost is elsewhere" -- plus the implied third, drawing no
    marker at all when the ghost is right there on screen. */
enum class GhostMarkerState
{
  /** The ghost is in this sector and on screen. The ghost itself is the
      marker; an arrow would only clutter it. */
  HIDDEN,
  /** In this sector, off screen: an arrow at the viewport edge, pointing at
      it, labelled with the signed time delta. */
  ARROW,
  /** In another sector. Deliberately NOT an arrow: positions in a visual
      track are sector-local, so the ghost's coordinates name a meaningless
      point in this sector and there is no honest direction to point. */
  OTHER_SECTOR,
};

/** How far inside the viewport an edge marker is placed, in world units.

    Not zero, and the reason is measured rather than aesthetic: clamped
    exactly to the edge, a marker is centred ON the boundary, so half the
    arrow and half the label fall outside the viewport and are never drawn.
    Observed in P4's visual pass -- the label read "ahead" with the start of
    "walk-ahead" cut off, and the arrow was a sliver at x = 0.

    Sized to clear the arrow (ARROW_TIP in ghost_manager.cpp) with room to
    spare; the label additionally picks its alignment from which edge it
    landed on, since no fixed margin can clear an arbitrarily long name. */
constexpr float MARKER_EDGE_MARGIN = 28.0f;

const char* to_string(GhostMarkerState state);

struct GhostMarker final
{
  GhostMarkerState state = GhostMarkerState::HIDDEN;
  /** Where to draw it, in the same coordinates the viewport is given in. */
  Vector clamped_pos = Vector(0.0f, 0.0f);
  /** Unit vector from the viewport centre toward the ghost. Zero in the
      HIDDEN and OTHER_SECTOR states, neither of which points anywhere. */
  Vector direction = Vector(0.0f, 0.0f);
  /** Signed, in seconds. Positive means the GHOST is ahead -- it reached
      where the live player is now in fewer steps. Only meaningful when
      delta_known. */
  float delta_seconds = 0.0f;
  bool delta_known = false;
};

/** Pure geometry: no engine state, no drawing, no clock.

    Separated from ghost_manager.cpp for the same reason GhostPlayback is
    separated from GhostObject -- ghost_manager.cpp includes Sector and
    cannot be linked into a unit test, and the marker's three-way decision is
    exactly the part worth testing directly. GhostMarkerTest links this file
    and math/rectf.cpp, and nothing else. */
GhostMarker compute_marker(const Rectf& viewport,
                           const Vector& ghost_pos,
                           bool same_sector,
                           float delta_seconds,
                           bool delta_known);

/* EOF */
