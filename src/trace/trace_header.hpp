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

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace trace {

class ByteReader;

enum class ProducerKind : uint8_t
{
  UNKNOWN = 0,
  HUMAN = 1,
  SCRIPT = 2,
  AGENT = 3
};

enum class Outcome : uint8_t
{
  INCOMPLETE = 0,
  COMPLETED = 1,
  DIED = 2,
  TIMEOUT = 3,
  ABORTED = 4
};

/** Mirrors Config::ScreenShakeMode. Declared here rather than included from
    gameconfig.hpp because src/trace/ is pure data; the mapping and the
    static_asserts that pin these three values to the engine's enum live in
    src/supertux/, the same arrangement trace_control_map.hpp uses for
    Control. */
enum class ScreenShakeMode : uint8_t
{
  OFF = 0,
  REDUCED = 1,
  FULL = 2
};

/** How the recorded episode's step 0 came to be.

    `UNKNOWN` is not a "missing value" -- it is the recorder saying it could
    not represent the session's start state, and it is the one value that
    forbids a `start` chunk. An executor must refuse it. */
enum class StartKind : uint8_t
{
  LEVEL_START = 0,
  RESPAWN = 1,
  CHECKPOINT = 2,
  UNKNOWN = 3
};

/** Episode-level metadata. Serialization order is the declaration order
    below. */
struct Header final
{
  std::string engine_version;
  std::string platform;
  std::string level_path;
  std::array<uint8_t, 16> level_md5 = {};

  /** Applied to gameRandom before the episode. */
  int32_t seed = 0;

  /** g_game_time at episode start. Restored before execution, because
      g_game_time is process-global and gameplay reads it absolutely. */
  float game_time_origin = 0.0f;

  /** Recorded, never assumed. */
  float step_rate = 0.0f;

  uint32_t step_count = 0;

  /** Derived from step_count; display only. */
  float play_time = 0.0f;

  ProducerKind producer_kind = ProducerKind::UNKNOWN;

  /** Opaque UTF-8. Never parsed by this library. */
  std::string provenance;

  Outcome outcome = Outcome::INCOMPLETE;

  uint32_t deaths = 0;
  uint32_t coins = 0;

  /** The viewport Camera::update sees (camera.cpp:401). A precondition of
      replay, not a diagnostic: VIDEO_NULL reports a hardcoded 1920x1080
      (null_video_system.cpp:30) that will not match a windowed recording, and
      the camera is a gameplay input. VIDEO_NULL is only selected under
      --resave (src/supertux/main.cpp:613-619), though -- an ordinary
      headless run (e.g. SDL_VIDEODRIVER=dummy) still goes through the normal
      SDL-backed video system and reports Config's window_size instead. See
      the spec's header-chunk section. */
  uint32_t viewport_width = 0;
  uint32_t viewport_height = 0;

  /** Gates both the earthquake (camera.cpp:517) and Camera::shake()
      (camera.cpp:325). */
  ScreenShakeMode screen_shake_mode = ScreenShakeMode::FULL;

  /** Scales camera peek (camera.cpp:593,638). Zero is a real setting. */
  float camera_peek_multiplier = 1.0f;

  /** What kind of start the episode's step 0 was, and whether it could be
      captured at all. UNKNOWN is the default because a default-constructed
      header has captured nothing: a producer that never looked at the session
      cannot claim the episode began at the level entrance. Every other value
      requires a `start` chunk -- see trace_file.cpp's validate(). */
  StartKind start_kind = StartKind::UNKNOWN;

  std::vector<uint8_t> encode() const;
  static Header decode(ByteReader& reader);
};

} // namespace trace

/* EOF */
