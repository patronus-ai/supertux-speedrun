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

#include "object/ghost_marker.hpp"

#include <algorithm>
#include <cmath>

const char*
to_string(GhostMarkerState state)
{
  switch (state)
  {
    case GhostMarkerState::HIDDEN: return "HIDDEN";
    case GhostMarkerState::ARROW: return "ARROW";
    case GhostMarkerState::OTHER_SECTOR: return "OTHER_SECTOR";
  }
  return "UNKNOWN";
}

GhostMarker
compute_marker(const Rectf& viewport,
               const Vector& ghost_pos,
               bool same_sector,
               float delta_seconds,
               bool delta_known)
{
  GhostMarker marker;

  // Checked BEFORE any use of ghost_pos, and that order is the whole point:
  // positions in a visual track are sector-local, so an off-sector ghost's
  // coordinates are a number in a different coordinate space. Deciding
  // "other sector" by whether those coordinates happen to land inside this
  // viewport would be reading a lie -- a ghost two sectors away would show
  // as HIDDEN whenever its unrelated x and y fell on screen.
  if (!same_sector)
  {
    marker.state = GhostMarkerState::OTHER_SECTOR;
    // Anchored at the top centre rather than at an edge: there is no honest
    // direction to point, so the marker says where the ghost is not, not
    // which way to walk.
    marker.clamped_pos = Vector(viewport.get_middle().x, viewport.get_top());
    return marker;
  }

  marker.delta_seconds = delta_seconds;
  marker.delta_known = delta_known;

  if (viewport.contains(ghost_pos))
  {
    // The ghost is drawing itself right there; an arrow on top of it would
    // be noise.
    marker.state = GhostMarkerState::HIDDEN;
    marker.clamped_pos = ghost_pos;
    return marker;
  }

  marker.state = GhostMarkerState::ARROW;
  // Inset by MARKER_EDGE_MARGIN rather than clamped to the edge itself, so
  // the arrow is drawn wholly inside the viewport instead of straddling its
  // boundary. See that constant for what straddling looked like.
  //
  // The margin is clamped against the viewport's own half-extent so a
  // viewport narrower than two margins still produces a position inside it
  // rather than an inverted range -- std::clamp with lo > hi is undefined
  // behaviour, not merely a wrong answer.
  const float margin_x =
    std::min(MARKER_EDGE_MARGIN, viewport.get_width() / 2.0f);
  const float margin_y =
    std::min(MARKER_EDGE_MARGIN, viewport.get_height() / 2.0f);
  marker.clamped_pos = Vector(
    std::clamp(ghost_pos.x, viewport.get_left() + margin_x,
               viewport.get_right() - margin_x),
    std::clamp(ghost_pos.y, viewport.get_top() + margin_y,
               viewport.get_bottom() - margin_y));

  const Vector centre = viewport.get_middle();
  const Vector offset = ghost_pos - centre;
  const float length = std::sqrt(offset.x * offset.x + offset.y * offset.y);
  // length can only be zero if the ghost is exactly at the centre, which
  // contains() already claimed -- but a degenerate (zero-size) viewport can
  // reach here, and dividing by it would produce a NaN direction that the
  // renderer would silently draw nothing for.
  marker.direction = (length > 0.0f) ? Vector(offset.x / length,
                                              offset.y / length)
                                     : Vector(0.0f, 0.0f);
  return marker;
}

/* EOF */
