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

namespace trace {

class ByteReader;

struct StepNote final
{
  uint32_t step = 0;
  std::vector<uint8_t> data;
};

/** Opaque metadata attached to a trace: one episode-level blob plus any number
    of sparse per-step blobs.

    These bytes are carried, never interpreted. Reward, value estimates and
    action distributions belong here, which is what keeps reinforcement-learning
    concerns out of the engine.

    Wire layout: uint32 episode_len, episode bytes, then repeated
    (uint32 step, uint32 len, bytes) until the payload is exhausted. */
class NoteStore final
{
public:
  NoteStore();

  void set_episode(std::vector<uint8_t> data);
  inline const std::vector<uint8_t>& episode() const { return m_episode; }

  void add_step(uint32_t step, std::vector<uint8_t> data);
  inline const std::vector<StepNote>& step_notes() const { return m_steps; }

  inline bool empty() const { return m_episode.empty() && m_steps.empty(); }

  std::vector<uint8_t> encode() const;
  static NoteStore decode(ByteReader& reader);

private:
  std::vector<uint8_t> m_episode;
  std::vector<StepNote> m_steps;
};

} // namespace trace

/* EOF */
