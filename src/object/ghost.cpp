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

#include "object/ghost.hpp"

#include <sstream>
#include <utility>

#include "object/camera.hpp"
#include "object/player.hpp"
#include "sprite/sprite.hpp"
#include "sprite/sprite_manager.hpp"
#include "supertux/resources.hpp"
#include "supertux/sector.hpp"
#include "util/log.hpp"
#include "video/drawing_context.hpp"
#include "video/video_system.hpp"
#include "video/viewport.hpp"
#include "video/layer.hpp"

GhostObject::GhostObject(std::shared_ptr<const trace::GhostPlayback> playback,
                         GhostStyle style,
                         const bool& render_enabled,
                         const std::string& sector_name,
                         uint32_t start_step) :
  m_playback(std::move(playback)),
  m_style(std::move(style)),
  m_render_enabled(render_enabled),
  m_sector_name(sector_name),
  m_step(start_step),
  // The live player's own sprite (Player derives from MovingSprite with this
  // path, player.cpp:161). A ghost IS a recording of Tux, so it draws Tux --
  // and the recorded action names are that sprite's action names, so any
  // other sprite would fail every has_action() lookup.
  m_sprite(SpriteManager::current()->create("images/creatures/tux/tux.sprite")),
  m_marker(),
  m_logged_state(GhostMarkerState::HIDDEN),
  m_marker_logged(false),
  m_logged_delta_known(false)
{
}

GhostObject::~GhostObject()
{
}

void
GhostObject::update(float dt_sec)
{
  // One step per call, deliberately ignoring dt_sec -- see the class comment.
  // This is the ghost's entire simulation: it advances a step index into a
  // recording. Nothing here reads or writes any other object, and nothing
  // here draws a random number, which is what makes
  // tools/trace/test_ghost_inertness_probe.py's byte-identical pair hold.
  m_step++;

  // The marker's state, computed HERE rather than in draw(). --steps has no
  // draw pass, so a state computed while drawing could not be observed by
  // any deterministic test and "all three states are reached" would rest on
  // an argument rather than on a measurement. Everything below is a read:
  // the camera's rect, the players' positions, and this ghost's own
  // playback. Verified by re-running the byte-identical probe with this in
  // place.
  const Sector& sector = Sector::get();
  // 0.6.3 has a single player: Sector::get_player() returns Player&, where
  // the fork this came from returns a vector (multiplayer). One player is
  // the only case here, so the emptiness check has nothing to guard.
  const Player& the_player = sector.get_player();

  // ...and no Camera::get_rect(). Build the viewport from the translation
  // and the screen size, which is what get_rect() does upstream.
  const Rectf viewport(sector.get_camera().get_translation(),
                       Sizef(static_cast<float>(SCREEN_WIDTH),
                             static_cast<float>(SCREEN_HEIGHT)));
  const Vector ghost_pos = current_pos();
  const bool same_sector = in_this_sector();

  float delta_seconds = 0.0f;
  bool delta_known = false;
  // The crossing search is a linear scan of the visual track, so it is done
  // only when the marker is actually going to show a time -- i.e. when the
  // ghost is in this sector and off screen. On screen the ghost IS the
  // marker and no delta is displayed, which is the common case and costs
  // nothing. The containment test is repeated inside compute_marker(),
  // deliberately: that function stays pure and decides the state itself,
  // and this is only deciding whether to pay for the scan.
  if (same_sector && !viewport.contains(ghost_pos))
  {
    const Vector player_pos = the_player.get_bbox().get_middle();
    const std::optional<uint32_t> crossed =
      m_playback->first_step_crossing_x(player_pos.x, m_sector_name);
    if (crossed)
    {
      // Positive means the GHOST is ahead: it reached where the live player
      // is now in fewer steps than the player has taken.
      delta_seconds = (static_cast<float>(m_step) -
                       static_cast<float>(*crossed)) /
                      trace::LOGICAL_STEP_RATE_HINT;
      delta_known = true;
    }
  }

  m_marker = compute_marker(viewport, ghost_pos, same_sector, delta_seconds,
                            delta_known);

  if (!m_marker_logged || m_marker.state != m_logged_state ||
      m_marker.delta_known != m_logged_delta_known)
  {
    m_marker_logged = true;
    m_logged_state = m_marker.state;
    m_logged_delta_known = m_marker.delta_known;
    // On change, not per step, and at log_info so an ordinary run stays
    // quiet. tools/trace/test_ghost_marker_probe.py reads these lines: they
    // are the only channel through which a headless test can see which
    // state was chosen, since nothing here renders to an inspectable
    // surface.
    // Composed into one string and emitted once: log_info stamps a fresh
    // "[INFO] file:line" prefix on every << statement, so building this
    // across several of them splits one record into three interleaved
    // lines and makes it ungreppable. Measured while adding the delta.
    std::ostringstream line;
    line << "ghost-marker: " << m_style.label << " "
         << to_string(m_marker.state) << " step=" << m_step << " delta=";
    if (m_marker.delta_known)
      line << m_marker.delta_seconds;
    else
      line << "unknown";
    log_info << line.str() << std::endl;
  }
}

void
GhostObject::draw(DrawingContext& context)
{
  // Desideratum 18: the switch gates DRAWING only. update() above keeps
  // advancing m_step regardless, so a ghost re-enabled later is still in the
  // right place rather than rewinding to where it was switched off.
  if (!m_render_enabled) return;

  // Positions in a visual track are sector-local, so an off-sector ghost's
  // coordinates name a meaningless point in THIS sector. It is reported by
  // the delta marker's "other sector" state instead of being drawn at a lie.
  if (!in_this_sector()) return;

  const trace::GhostPose pose =
    // 0.6.3 has no frame prediction, so DrawingContext has no time offset and
    // there is no sub-step remainder to interpolate across: a ghost is drawn
    // exactly on its step. Upstream this is context.get_time_offset(), which
    // is nonzero only when frame_prediction is on -- so 0.0f here is the same
    // value that build produces with the feature off, not a degradation.
    m_playback->pose_at(m_step, 0.0f);
  if (!(pose.flags & trace::VisualFlags::VISIBLE)) return;

  const std::string& action = m_playback->action_name(pose);
  if (!action.empty() && m_sprite->has_action(action))
  {
    // Seeking must set BOTH frame fields, not just the action. Every
    // Sprite::set_action resets the frame whenever the action *family*
    // changes, so set_action alone would put every seek at frame 0 -- which
    // is precisely the frozen pose this phase refuses to render from a
    // trace. See the spec's Layer 3.
    m_sprite->set_action(action);
    m_sprite->set_frame(pose.frame_idx);
    m_sprite->set_frame_progress(pose.frame_progress);
  }
  // Sprite::update() is deliberately NOT called: a ghost's animation comes
  // entirely from the recorded frame_idx/frame_progress, so advancing the
  // sprite's own clock would fight the trace. This is the difference between
  // a ghost and a live object, and it is why those recorded fields had to be
  // made real in P3e-2 before this phase could exist.
  m_sprite->set_color(m_style.tint);
  m_sprite->set_alpha(m_style.alpha);

  // LAYER_OBJECTS - 1: behind the live player, so a ghost overlapping Tux
  // never hides the thing the player is controlling.
  const Vector pos(pose.pos_x, pose.pos_y);
  m_sprite->draw(context.color(), pos, LAYER_OBJECTS - 1);

  // Desideratum 16 asks for k simultaneous ghosts "with distinguishable
  // color AND label", and until this the label existed only on the edge
  // marker -- which is shown precisely when the ghost is NOT on screen. A
  // ghost you could actually see was identified by its tint alone, which
  // stops working as soon as two of them are visible at once, or the
  // level's own palette happens to be near a ghost's colour.
  //
  // Drawn in world coordinates, because this runs inside the sector's
  // camera transform -- unlike GhostManager::draw_markers, which runs after
  // that transform has been popped and subtracts the camera translation
  // itself.
  // NAME_GAP above the sprite's top edge, not flush against it: at zero the
  // descenders sat on the ghost's head and the outline of each ran into the
  // other. Measured by screenshot, which is the only way this could have
  // been noticed.
  constexpr float NAME_GAP = 6.0f;
  const Vector name_pos(pos.x + static_cast<float>(m_sprite->get_width()) / 2.0f,
                        pos.y - Resources::normal_font->get_height() - NAME_GAP);
  context.color().draw_text(Resources::normal_font, m_style.label, name_pos,
                            FontAlignment::ALIGN_CENTER, LAYER_OBJECTS - 1,
                            m_style.tint);
}

bool
GhostObject::in_this_sector() const
{
  return m_playback->sector_at(m_step) == m_sector_name;
}

Vector
GhostObject::current_pos() const
{
  const trace::GhostPose pose = m_playback->pose_at(m_step, 0.0f);
  return Vector(pose.pos_x, pose.pos_y);
}

/* EOF */
