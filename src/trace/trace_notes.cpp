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

#include "trace/trace_notes.hpp"

#include <utility>

#include "trace/trace_bytes.hpp"
#include "trace/trace_error.hpp"

namespace trace {

NoteStore::NoteStore() :
  m_episode(),
  m_steps()
{
}

void
NoteStore::set_episode(std::vector<uint8_t> data)
{
  m_episode = std::move(data);
}

void
NoteStore::add_step(uint32_t step, std::vector<uint8_t> data)
{
  StepNote note;
  note.step = step;
  note.data = std::move(data);
  m_steps.push_back(std::move(note));
}

std::vector<uint8_t>
NoteStore::encode() const
{
  ByteWriter writer;

  writer.u32(static_cast<uint32_t>(m_episode.size()));
  if (!m_episode.empty())
    writer.bytes(m_episode.data(), m_episode.size());

  for (const StepNote& note : m_steps)
  {
    writer.u32(note.step);
    writer.u32(static_cast<uint32_t>(note.data.size()));
    if (!note.data.empty())
      writer.bytes(note.data.data(), note.data.size());
  }

  return writer.data();
}

NoteStore
NoteStore::decode(ByteReader& reader)
{
  NoteStore store;

  if (reader.exhausted())
    return store;

  const uint32_t episode_len = reader.u32();
  if (episode_len > reader.remaining())
    throw ParseError("note episode blob is truncated");

  store.m_episode.resize(episode_len);
  if (episode_len > 0)
    reader.bytes(store.m_episode.data(), episode_len);

  while (!reader.exhausted())
  {
    StepNote note;
    note.step = reader.u32();

    const uint32_t len = reader.u32();
    if (len > reader.remaining())
      throw ParseError("note step blob is truncated");

    note.data.resize(len);
    if (len > 0)
      reader.bytes(note.data.data(), len);

    store.m_steps.push_back(std::move(note));
  }

  return store;
}

} // namespace trace

/* EOF */
