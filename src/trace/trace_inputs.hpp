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

#include <cstdint>
#include <vector>

#include "trace/trace_action.hpp"

namespace trace {

class ByteReader;

/** One action mask per logical step — the ground truth of a trace.

    Stored flat so at() is O(1); run-length encoded only on the wire, where
    held buttons collapse to a handful of bytes.

    Wire layout: repeated (uint16 mask, uint16 run_length), run_length >= 1. */
class InputTrack final
{
public:
  static constexpr uint32_t MAX_RUN_LENGTH = 65535;

  InputTrack();

  /** Throws ParseError when any reserved bit is set. On throw the track is
      unchanged. */
  void push(ActionMask mask);

  /** Throws ParseError when step is out of range. */
  ActionMask at(uint32_t step) const;

  inline uint32_t step_count() const
  {
    return static_cast<uint32_t>(m_masks.size());
  }

  std::vector<uint8_t> encode() const;

  /** Reads until the reader is exhausted, then checks the decoded step count
      against expected_steps. */
  static InputTrack decode(ByteReader& reader, uint32_t expected_steps);

private:
  std::vector<ActionMask> m_masks;
};

} // namespace trace

/* EOF */
