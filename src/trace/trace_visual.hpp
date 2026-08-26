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

#include <cstddef>
#include <cstdint>
#include <vector>

namespace trace {

class ByteReader;

/** Bytes on the wire per visual frame. Fixed, so frame n lives at offset
    VISUAL_FRAME_BYTES * n and step addressing is O(1). */
constexpr size_t VISUAL_FRAME_BYTES = 21;

enum VisualFlags : uint8_t
{
  FACING_RIGHT = 1 << 0,
  VISIBLE      = 1 << 1,
  ACTIVE       = 1 << 2,
  DEAD         = 1 << 3
};

/** Visual state of the recorded player at one logical step.

    This is derived data: it is materialized by executing the input track, and
    exists so playback needs no physics. Field order here is the serialization
    order. */
struct VisualFrame final
{
  float pos_x = 0.0f;
  float pos_y = 0.0f;
  float vel_x = 0.0f;
  float vel_y = 0.0f;
  uint16_t action_id = 0;
  uint8_t frame_idx = 0;
  uint8_t frame_progress = 0;
  uint8_t flags = 0;
};

/** Maps a sprite frame progress in [0, 1) onto a byte, clamping outside that
    range. */
uint8_t quantize_progress(float progress);
float dequantize_progress(uint8_t quantized);

class VisualTrack final
{
public:
  VisualTrack();

  void push(const VisualFrame& frame);

  /** Throws ParseError when step is out of range. */
  const VisualFrame& at(uint32_t step) const;

  inline uint32_t step_count() const
  {
    return static_cast<uint32_t>(m_frames.size());
  }

  std::vector<uint8_t> encode() const;
  static VisualTrack decode(ByteReader& reader, uint32_t expected_steps);

private:
  std::vector<VisualFrame> m_frames;
};

} // namespace trace

/* EOF */
