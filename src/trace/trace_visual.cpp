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

#include "trace/trace_visual.hpp"

#include <cmath>

#include "trace/trace_bytes.hpp"
#include "trace/trace_error.hpp"

namespace trace {

uint8_t
quantize_progress(float progress)
{
  if (!(progress > 0.0f))  // also catches NaN
    return 0;

  const float scaled = std::floor(progress * 256.0f);
  if (scaled >= 255.0f)
    return 255;

  return static_cast<uint8_t>(scaled);
}

float
dequantize_progress(uint8_t quantized)
{
  return static_cast<float>(quantized) / 256.0f;
}

VisualTrack::VisualTrack() :
  m_frames()
{
}

void
VisualTrack::push(const VisualFrame& frame)
{
  m_frames.push_back(frame);
}

const VisualFrame&
VisualTrack::at(uint32_t step) const
{
  if (static_cast<size_t>(step) >= m_frames.size())
    throw ParseError("visual step out of range");

  return m_frames[step];
}

std::vector<uint8_t>
VisualTrack::encode() const
{
  ByteWriter writer;
  for (const VisualFrame& frame : m_frames)
  {
    writer.f32(frame.pos_x);
    writer.f32(frame.pos_y);
    writer.f32(frame.vel_x);
    writer.f32(frame.vel_y);
    writer.u16(frame.action_id);
    writer.u8(frame.frame_idx);
    writer.u8(frame.frame_progress);
    writer.u8(frame.flags);
  }

  return writer.data();
}

VisualTrack
VisualTrack::decode(ByteReader& reader, uint32_t expected_steps)
{
  if (reader.remaining() % VISUAL_FRAME_BYTES != 0)
    throw ParseError("visual payload is not a whole number of frames");

  VisualTrack track;
  while (!reader.exhausted())
  {
    VisualFrame frame;
    frame.pos_x = reader.f32();
    frame.pos_y = reader.f32();
    frame.vel_x = reader.f32();
    frame.vel_y = reader.f32();
    frame.action_id = reader.u16();
    frame.frame_idx = reader.u8();
    frame.frame_progress = reader.u8();
    frame.flags = reader.u8();
    track.m_frames.push_back(frame);
  }

  if (track.step_count() != expected_steps)
    throw ParseError("visual step count disagrees with the header");

  return track;
}

} // namespace trace

/* EOF */
