//  SuperTux
//  Copyright (C) 2021 A. Semphris <semphris@protonmail.com>
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

// Export functions for emscripten
#ifdef __EMSCRIPTEN__

#include <emscripten.h>
#include <emscripten/html5.h>

#include "addon/addon_manager.hpp"
#include "audio/sound_manager.hpp"
#include "gui/menu_manager.hpp"
#include "supertux/gameconfig.hpp"
#include "supertux/globals.hpp"
#include "video/video_system.hpp"

// Explicit, rather than relying on main.cpp's include order: the pre-existing st_tux_* helpers
// happened to compile because Sector/Player were already visible at the include site
// (main.cpp:52), which is fragile. st_tick()/st_state_hash() need ScreenManager and
// MovingObject, neither guaranteed to be transitively present.
#include "object/player.hpp"
#include "supertux/game_session.hpp"
#include "supertux/moving_object.hpp"
#include "supertux/screen_manager.hpp"
#include "supertux/sector.hpp"

extern "C" {

void set_resolution(int w, int h);
void save_config();
void init_emscripten();
void onDownloadProgress(int id, int loaded, int total);
void onDownloadFinished(int id);
void onDownloadError(int id);
void onDownloadAborted(int id);

// ---------------------------------------------------------------------------------------
// BENCHMARK OBSERVABILITY. Exported so a harness can read progress directly instead of
// scraping pixels or scanning the wasm heap for a plausible-looking float. Without these the
// only signal is the canvas, which cannot distinguish "stuck in a menu" from "playing badly".
// Returns -1 when there is no active session, which is itself the in-level check a harness
// needs before it starts measuring anything.
EMSCRIPTEN_KEEPALIVE
int
st_step_count()
{
  // Monotonic count of LOGICAL steps executed. With SUPERTUX_DETERMINISTIC=1 each loop
  // iteration runs exactly one step, so this is the game's own clock -- and the only correct
  // thing for a harness to index inputs against. Driving input by wall-clock milliseconds
  // leaves the step COUNT per keypress varying with machine load, which is nondeterministic
  // even when every individual step is fixed-size.
  extern unsigned int g_deterministic_steps;
  return static_cast<int>(g_deterministic_steps);
}

// ---------------------------------------------------------------------------------------
// SYNCHRONOUS DRIVING. Under emscripten the loop is scheduled by the browser
// (screen_manager.cpp:645 emscripten_set_main_loop(g_loop_iter, -1, 1) -> rAF), so a harness
// can only POLL st_step_count and hope to catch each step. That is not good enough: an input
// meant for step 200 lands wherever the poll happens to fall, and the observed step count
// varies run to run (we measured 1180-1199 for a fixed 1200-step target). The jitter is in the
// OBSERVER, and it makes engine determinism unmeasurable either way.
//
// st_pause_main_loop() hands scheduling to the harness; st_tick() then runs exactly one
// iteration synchronously. Inputs land on exact steps by construction and the step count is
// identical every run because the harness, not the browser, decides it.
EMSCRIPTEN_KEEPALIVE
void
st_pause_main_loop()
{
  emscripten_cancel_main_loop();
}

EMSCRIPTEN_KEEPALIVE
int
st_tick()
{
  auto* sm = ::ScreenManager::current();
  if (!sm) return -1;
  sm->loop_iter();                    // public (screen_manager.hpp:60)
  extern unsigned int g_deterministic_steps;
  return static_cast<int>(g_deterministic_steps);
}

// A state fingerprint over EVERY moving object, not just Tux. Needed because a no-input
// control run leaves Tux motionless: his position is then one constant value at every step in
// every run, so comparing it across runs is trivially identical and proves nothing. The
// level's other actors (snowballs and friends) keep moving regardless of input, so this gives
// a control run something real to disagree about.
EMSCRIPTEN_KEEPALIVE
unsigned int
st_state_hash()
{
  auto* s = ::Sector::current();
  if (!s) return 0u;
  unsigned int h = 2166136261u;                 // FNV-1a over quantised positions
  auto mix = [&h](unsigned int v) {
    h ^= v;
    h *= 16777619u;
  };
  for (const auto& obj : s->get_objects())
  {
    auto* mo = dynamic_cast<::MovingObject*>(obj.get());
    if (!mo) continue;
    // Quantise to 1/256 px: exact float bits would make the hash sensitive to harmless
    // last-bit noise, which would report divergence that does not affect gameplay.
    const auto p = mo->get_pos();
    mix(static_cast<unsigned int>(static_cast<int>(p.x * 256.0f)));
    mix(static_cast<unsigned int>(static_cast<int>(p.y * 256.0f)));
  }
  return h;
}

// TERMINAL CONDITIONS. A speedrun objective needs to know the run ENDED and how: position
// cannot distinguish "reached the goal" from "standing next to it", and a scorer that only reads
// x/y silently rewards loitering near the exit. goal_frame is simply st_step_count() at the
// moment st_level_finished() first returns 1.
// st_goal_reached() is the SPEEDRUN clock: it flips the moment Tux touches the goal and the end
// sequence is created. st_level_finished() flips only after the victory animation completes
// (check_end_conditions -> m_end_sequence->is_done() -> finish()), so timing to it would add the
// animation's length to every run and make the metric depend on animation duration.
// DEBUG/VERIFICATION ONLY -- never used by the scorer. Place Tux at a position so the goal
// trigger can be exercised without playing the whole level: welcome_antarctica's endsequence
// sequencetrigger sits at x=8960, far past where any blind tape survives (ours died at x~1284).
// Warping next to it drives the REAL chain (sequencetrigger -> start_sequence -> m_end_sequence
// created -> st_goal_reached), which is what needs proving; asserting the flag is correct from
// reading the source is not the same as watching it fire.
EMSCRIPTEN_KEEPALIVE
void
st_debug_warp(float x, float y)
{
  auto* s = ::Sector::current();
  if (!s || !s->get_object_count<::Player>()) return;
  // Sector::activate(pos), NOT Player::set_pos(). set_pos moves Tux but leaves the CAMERA
  // behind, putting him outside the sector's active region -- he then stops being simulated
  // entirely: warped to x=8880 his position never changed again for 975 steps, not even under
  // gravity. activate() is what teleporters use; it repositions player and camera together.
  s->activate(::Vector(x, y));
}

EMSCRIPTEN_KEEPALIVE
int
st_goal_reached()
{
  auto* gs = ::GameSession::current();
  if (!gs) return -1;                 // -1 = no session, distinct from 0 = not yet
  return gs->is_goal_reached() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int
st_level_finished()
{
  auto* gs = ::GameSession::current();
  if (!gs) return -1;
  return gs->is_level_finished() ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE
int
st_player_dead()
{
  auto* s = ::Sector::current();
  if (!s || !s->get_object_count<::Player>()) return -1;
  const auto& p = s->get_player();
  // is_dying() covers the death ANIMATION, which starts before is_dead(). A harness that waits
  // only for is_dead() keeps feeding inputs into a run already lost -- the same late-terminal
  // trap seen in Fireboy & Watergirl, where the animation terminal fired ~90 frames early.
  return (p.is_dead() || p.is_dying()) ? 1 : 0;
}

// Preserve the browser host ABI, but this benchmark packages no audio assets. Calls that used
// to unmute must therefore leave both managers disabled instead of triggering missing-file
// lookups throughout the level.
EMSCRIPTEN_KEEPALIVE
void
st_set_muted(int /*muted*/)
{
  auto* sound = ::SoundManager::current();
  if (!sound) return;

  sound->stop_sounds();
  sound->enable_sound(false);
  sound->enable_music(false);
}

EMSCRIPTEN_KEEPALIVE
float
st_tux_x()
{
  auto* s = ::Sector::current();
  if (!s) return -1.0f;
  // This revision of Sector exposes `Player& get_player()` (sector.hpp:150), not a
  // get_players() container -- guard on the count first, since the reference accessor
  // has nothing safe to return when no player exists.
  auto* p = s->get_object_count<::Player>() ? &s->get_player() : nullptr;
  return p ? p->get_pos().x : -1.0f;
}

EMSCRIPTEN_KEEPALIVE
float
st_tux_y()
{
  auto* s = ::Sector::current();
  if (!s) return -1.0f;
  // This revision of Sector exposes `Player& get_player()` (sector.hpp:150), not a
  // get_players() container -- guard on the count first, since the reference accessor
  // has nothing safe to return when no player exists.
  auto* p = s->get_object_count<::Player>() ? &s->get_player() : nullptr;
  return p ? p->get_pos().y : -1.0f;
}

EMSCRIPTEN_KEEPALIVE
int
st_in_level()
{
  return ::Sector::current() ? 1 : 0;
}

// Stronger than st_in_level(): Sector::current() already exists while the non-interactive
// level-intro title card is on top of the session. Hosts must wait for this signal before
// pausing the main loop and declaring that player input can begin.
EMSCRIPTEN_KEEPALIVE
int
st_gameplay_ready()
{
  auto* gs = ::GameSession::current();
  return (gs && gs->is_active()) ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE // This is probably not useful, I just want ppl to know it exists
void
set_resolution(int w, int h)
{
  VideoSystem::current()->on_resize(w, h);
  MenuManager::instance().on_window_resize();
}

EMSCRIPTEN_KEEPALIVE // Same as above
void
save_config()
{
  g_config->save();
}

void
onDownloadProgress(int id, int loaded, int total)
{
  AddonManager::current()->onDownloadProgress(id, loaded, total);
}

void
onDownloadFinished(int id)
{
  AddonManager::current()->onDownloadFinished(id);
}

void
onDownloadError(int id)
{
  AddonManager::current()->onDownloadError(id);
}

void
onDownloadAborted(int id)
{
  AddonManager::current()->onDownloadAborted(id);
}

} // extern "C"

void
init_emscripten()
{
  EM_ASM({
    if (window.supertux_onready)
      window.supertux_onready();
  }, 0); // EM_ASM is a variadic macro and Clang requires at least 1 value for the variadic argument
}

#endif
