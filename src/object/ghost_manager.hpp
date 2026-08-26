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
#include <vector>

#include "object/ghost.hpp"

class DrawingContext;
class Sector;

/** Owns k ghosts: their playbacks, their colors and labels, the render
    switch, and the edge-of-viewport delta markers.

    It owns the *playbacks*, not the GhostObjects. A Sector owns its objects,
    and a sector change destroys them, so the objects are re-created on every
    sector activation from playbacks that outlive them -- which is also why
    GhostStyle lives here: a ghost that changed colour every time the player
    walked through a door would not be the same ghost. */
class GhostManager final
{
public:
  GhostManager();
  ~GhostManager();

  /** Loads every path given. Throws std::runtime_error naming the offending
      file on the first failure: a ghost the user asked for and did not get
      must not be silent. Both of GhostPlayback's refusals -- no visual
      track, frozen sprite fields -- surface through here. */
  void load(const std::vector<std::string>& paths);

  /** Suffixes the most recently load()-ed ghost's on-screen label.

      For a store match the ghost library's rule (trace::StoreVerdict)
      returns REPLAY_STALE for: the level has changed underneath the trace.
      The design's whole marking is a log line -- already emitted by
      trace::ghost_store_matches(), the one place that has both digests to
      hand -- plus this suffix, so what is on screen is honest without
      reading a log. A no-op if nothing has been loaded yet, so a caller
      that marks a load() it made without checking it succeeded degrades
      rather than crashing. */
  void mark_last_stale();

  /** Adds one GhostObject per loaded ghost to the sector, each resuming at
      the step its predecessor had reached. Called after the sector is
      activated, on every restart AND every sector change -- a Sector owns
      its objects, so a transition leaves the previous GhostObjects behind
      in a sector that is no longer being updated.

      `restart` says which of the two this is. A restart puts the ghosts
      back to step 0, because the player's attempt started over and a ghost
      is a racing opponent, not a spectator. A sector transition does NOT:
      the ghost's clock runs from the start of timing, so it keeps
      counting. */
  void attach_to_sector(Sector& sector, const std::string& sector_name,
                        bool restart);

  inline bool empty() const { return m_playbacks.empty(); }
  inline size_t size() const { return m_playbacks.size(); }

  /** Desideratum 18: the render switch. It gates DRAWING only -- attached
      ghosts keep updating and keep advancing their step clocks while it is
      off, so a ghost switched back on is still in the right place. */
  inline void set_render_enabled(bool enabled) { m_render_enabled = enabled; }
  inline bool render_enabled() const { return m_render_enabled; }
  inline const bool& render_enabled_ref() const { return m_render_enabled; }

  /** The separate pass (desideratum 18), called from GameSession::draw after
      the sector's own draw rather than from inside it, so that disabling
      ghost rendering disables nothing else. */
  void draw_markers(DrawingContext& context, Sector& sector);

private:
  std::vector<std::shared_ptr<const trace::GhostPlayback>> m_playbacks;
  std::vector<GhostStyle> m_styles;
  bool m_render_enabled;
  /** The objects added to the currently active sector, so the marker pass
      can ask each where it is. Non-owning: the Sector owns them, and this
      vector is cleared and rebuilt by every attach_to_sector(). */
  std::vector<GhostObject*> m_attached;
  /** Each ghost's step clock, carried across the re-attach a sector change
      forces. Without it every transition would rewind every ghost to step
      0, which looks like the ghost restarting the level. */
  std::vector<uint32_t> m_steps;

private:
  GhostManager(const GhostManager&) = delete;
  GhostManager& operator=(const GhostManager&) = delete;
};

/* EOF */
