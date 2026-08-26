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

#include "trace/trace_inputs.hpp"

#include "trace/trace_bytes.hpp"
#include "trace/trace_error.hpp"

namespace trace {

InputTrack::InputTrack() :
  m_masks()
{
}

void
InputTrack::push(ActionMask mask)
{
  if ((mask & RESERVED_MASK) != 0)
    throw ParseError("action mask sets a reserved bit");

  m_masks.push_back(mask);
}

ActionMask
InputTrack::at(uint32_t step) const
{
  if (static_cast<size_t>(step) >= m_masks.size())
    throw ParseError("input step out of range");

  return m_masks[step];
}

std::vector<uint8_t>
InputTrack::encode() const
{
  ByteWriter writer;

  size_t i = 0;
  while (i < m_masks.size())
  {
    const ActionMask mask = m_masks[i];

    uint32_t run = 1;
    while (i + run < m_masks.size() &&
           m_masks[i + run] == mask &&
           run < MAX_RUN_LENGTH)
    {
      ++run;
    }

    writer.u16(mask);
    writer.u16(static_cast<uint16_t>(run));
    i += run;
  }

  return writer.data();
}

InputTrack
InputTrack::decode(ByteReader& reader, uint32_t expected_steps)
{
  InputTrack track;

  // Running total, checked *inside* the loop. Each 4-byte entry expands to up
  // to 65535 masks, so comparing only the final total against expected_steps
  // would let an 86-byte file materialize 13 million masks (115 MB) before
  // any bound applies -- and a std::bad_alloc from that growth would escape
  // deserialize() uncaught, breaking the "every decode path throws
  // ParseError" contract. uint64_t, not size_t: on a 32-bit host a hostile
  // expected_steps near UINT32_MAX plus one run could wrap a 32-bit counter.
  uint64_t total = 0;

  while (!reader.exhausted())
  {
    const ActionMask mask = reader.u16();
    const uint16_t run = reader.u16();

    if (run == 0)
      throw ParseError("input run length must be at least 1");

    if ((mask & RESERVED_MASK) != 0)
      throw ParseError("action mask sets a reserved bit");

    total += run;
    if (total > expected_steps)
      throw ParseError("input run lengths exceed the header step count");

    for (uint16_t n = 0; n < run; ++n)
      track.m_masks.push_back(mask);
  }

  if (track.step_count() != expected_steps)
    throw ParseError("input step count disagrees with the header");

  return track;
}

} // namespace trace

/* EOF */
