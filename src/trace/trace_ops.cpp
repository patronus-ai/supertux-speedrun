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

#include "trace/trace_ops.hpp"

#include <vector>

#include "trace/trace_error.hpp"

namespace trace {

namespace {

/** Index of the transition governing step, or npos when there is none.

    Assumes transitions is strictly increasing by first_step, as validate()
    requires; the early break below is only correct under that assumption.
    Given an unsorted vector this silently returns the wrong index. */
size_t governing_transition(const std::vector<SectorTransition>& transitions,
                           uint32_t step)
{
  size_t found = transitions.size();
  for (size_t i = 0; i < transitions.size(); ++i)
  {
    if (transitions[i].first_step <= step)
      found = i;
    else
      break;
  }
  return found;
}

} // namespace

Trace
slice(const Trace& source, uint32_t begin, uint32_t end)
{
  if (begin > end)
    throw ParseError("slice begin is after end");

  if (end > source.header.step_count)
    throw ParseError("slice end is past the end of the trace");

  const uint32_t count = end - begin;

  Trace out;
  out.header = source.header;
  out.header.step_count = count;
  out.header.play_time = (source.header.step_rate > 0.0f)
    ? static_cast<float>(count) / source.header.step_rate
    : 0.0f;

  if (end != source.header.step_count)
    out.header.outcome = Outcome::INCOMPLETE;

  for (uint32_t i = begin; i < end; ++i)
    out.inputs.push(source.inputs.at(i));

  out.actions = source.actions;
  out.sectors = source.sectors;
  out.notes.set_episode(source.notes.episode());

  for (const StepNote& note : source.notes.step_notes())
  {
    if (note.step >= begin && note.step < end)
      out.notes.add_step(note.step - begin, note.data);
  }

  out.unknown_chunks = source.unknown_chunks;

  if (!source.has_visual || count == 0)
    return out;

  out.has_visual = true;
  for (uint32_t i = begin; i < end; ++i)
    out.visual.push(source.visual.at(i));

  const size_t governing = governing_transition(source.sector_transitions, begin);
  if (governing == source.sector_transitions.size())
    throw ParseError("no sector transition governs the slice start");

  SectorTransition first;
  first.sector_id = source.sector_transitions[governing].sector_id;
  first.first_step = 0;
  out.sector_transitions.push_back(first);

  for (size_t i = governing + 1; i < source.sector_transitions.size(); ++i)
  {
    const SectorTransition& transition = source.sector_transitions[i];
    if (transition.first_step >= end)
      break;

    SectorTransition shifted;
    shifted.sector_id = transition.sector_id;
    shifted.first_step = transition.first_step - begin;
    out.sector_transitions.push_back(shifted);
  }

  return out;
}

Trace
truncate(const Trace& source, uint32_t steps)
{
  return slice(source, 0, steps);
}

Trace
concat(const Trace& first, const Trace& second)
{
  if (first.header.level_path != second.header.level_path)
    throw ParseError("cannot concatenate traces of different levels");

  if (first.header.level_md5 != second.header.level_md5)
    throw ParseError("cannot concatenate traces with different level hashes");

  if (first.header.step_rate != second.header.step_rate)
    throw ParseError("cannot concatenate traces with different step rates");

  if (first.header.seed != second.header.seed)
    throw ParseError("cannot concatenate traces with different seeds");

  if (first.has_visual != second.has_visual)
    throw ParseError("cannot concatenate traces that disagree on a visual track");

  Trace out;
  out.header = first.header;
  out.header.step_count = first.header.step_count + second.header.step_count;
  out.header.play_time = (first.header.step_rate > 0.0f)
    ? static_cast<float>(out.header.step_count) / first.header.step_rate
    : 0.0f;
  out.header.outcome = second.header.outcome;
  out.header.deaths = first.header.deaths + second.header.deaths;
  out.header.coins = first.header.coins + second.header.coins;

  for (uint32_t i = 0; i < first.inputs.step_count(); ++i)
    out.inputs.push(first.inputs.at(i));
  for (uint32_t i = 0; i < second.inputs.step_count(); ++i)
    out.inputs.push(second.inputs.at(i));

  out.actions = first.actions;
  out.sectors = first.sectors;

  std::vector<uint16_t> action_remap;
  for (size_t i = 0; i < second.actions.size(); ++i)
    action_remap.push_back(out.actions.intern(second.actions.get(static_cast<uint16_t>(i))));

  std::vector<uint16_t> sector_remap;
  for (size_t i = 0; i < second.sectors.size(); ++i)
    sector_remap.push_back(out.sectors.intern(second.sectors.get(static_cast<uint16_t>(i))));

  out.notes.set_episode(first.notes.episode());
  for (const StepNote& note : first.notes.step_notes())
    out.notes.add_step(note.step, note.data);
  for (const StepNote& note : second.notes.step_notes())
    out.notes.add_step(note.step + first.header.step_count, note.data);

  out.unknown_chunks = first.unknown_chunks;
  out.unknown_chunks.insert(out.unknown_chunks.end(),
                            second.unknown_chunks.begin(),
                            second.unknown_chunks.end());

  if (!first.has_visual)
    return out;

  out.has_visual = true;
  for (uint32_t i = 0; i < first.visual.step_count(); ++i)
    out.visual.push(first.visual.at(i));

  for (uint32_t i = 0; i < second.visual.step_count(); ++i)
  {
    VisualFrame frame = second.visual.at(i);
    if (static_cast<size_t>(frame.action_id) >= action_remap.size())
      throw ParseError("second trace references an unknown action id");

    frame.action_id = action_remap[frame.action_id];
    out.visual.push(frame);
  }

  out.sector_transitions = first.sector_transitions;
  for (const SectorTransition& transition : second.sector_transitions)
  {
    if (static_cast<size_t>(transition.sector_id) >= sector_remap.size())
      throw ParseError("second trace references an unknown sector id");

    SectorTransition shifted;
    shifted.sector_id = sector_remap[transition.sector_id];
    shifted.first_step = transition.first_step + first.header.step_count;
    out.sector_transitions.push_back(shifted);
  }

  return out;
}

} // namespace trace

/* EOF */
