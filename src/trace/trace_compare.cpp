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

#include "trace/trace_compare.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace trace {

namespace {

/** Float equality by bit pattern. `==` is wrong twice over: NaN != NaN would
    report two identical traces as divergent, and -0.0 == 0.0 would report
    two different ones as equal. The determinism claim is byte-identical
    output, so bits are what it means. */
bool same_bits(float a, float b)
{
  uint32_t ia = 0, ib = 0;
  std::memcpy(&ia, &a, sizeof(ia));
  std::memcpy(&ib, &b, sizeof(ib));
  return ia == ib;
}

std::string render(float v)
{
  std::ostringstream os;
  os << std::setprecision(9) << v;
  return os.str();
}

std::string render(bool v)
{
  return v ? "true" : "false";
}

std::string render(int32_t v)
{
  return std::to_string(v);
}

std::string render(uint32_t v)
{
  return std::to_string(v);
}

std::string render(const std::array<uint8_t, 16>& digest)
{
  static const char* const HEX = "0123456789abcdef";
  std::string out;
  out.reserve(digest.size() * 2);
  for (uint8_t byte : digest)
  {
    out.push_back(HEX[byte >> 4]);
    out.push_back(HEX[byte & 0x0F]);
  }
  return out;
}

const char* producer_kind_name(ProducerKind v)
{
  switch (v)
  {
    case ProducerKind::UNKNOWN: return "UNKNOWN";
    case ProducerKind::HUMAN: return "HUMAN";
    case ProducerKind::SCRIPT: return "SCRIPT";
    case ProducerKind::AGENT: return "AGENT";
  }
  return "?"; // guards a value outside the declared range -- reachable if this
              // is ever handed a byte decoded from a corrupt or forward-
              // incompatible file rather than one produced by this library
}

const char* outcome_name(Outcome v)
{
  switch (v)
  {
    case Outcome::INCOMPLETE: return "INCOMPLETE";
    case Outcome::COMPLETED: return "COMPLETED";
    case Outcome::DIED: return "DIED";
    case Outcome::TIMEOUT: return "TIMEOUT";
    case Outcome::ABORTED: return "ABORTED";
  }
  return "?"; // guards a value outside the declared range -- reachable if this
              // is ever handed a byte decoded from a corrupt or forward-
              // incompatible file rather than one produced by this library
}

const char* screen_shake_mode_name(ScreenShakeMode v)
{
  switch (v)
  {
    case ScreenShakeMode::OFF: return "OFF";
    case ScreenShakeMode::REDUCED: return "REDUCED";
    case ScreenShakeMode::FULL: return "FULL";
  }
  return "?"; // guards a value outside the declared range -- reachable if this
              // is ever handed a byte decoded from a corrupt or forward-
              // incompatible file rather than one produced by this library
}

const char* start_kind_name(StartKind v)
{
  switch (v)
  {
    case StartKind::LEVEL_START: return "LEVEL_START";
    case StartKind::RESPAWN: return "RESPAWN";
    case StartKind::CHECKPOINT: return "CHECKPOINT";
    case StartKind::UNKNOWN: return "UNKNOWN";
  }
  return "?"; // guards a value outside the declared range -- reachable if this
              // is ever handed a byte decoded from a corrupt or forward-
              // incompatible file rather than one produced by this library
}

const char* bonus_name(Bonus v)
{
  switch (v)
  {
    case Bonus::NONE: return "NONE";
    case Bonus::GROWUP: return "GROWUP";
    case Bonus::FIRE: return "FIRE";
    case Bonus::ICE: return "ICE";
    case Bonus::AIR: return "AIR";
    case Bonus::EARTH: return "EARTH";
  }
  return "?"; // guards a value outside the declared range -- reachable if this
              // is ever handed a byte decoded from a corrupt or forward-
              // incompatible file rather than one produced by this library
}

/** Enum fields render as both their numeric value and their name, so a
    printed difference is legible without cross-referencing the header. */
std::string render(ProducerKind v)
{
  return std::to_string(static_cast<uint8_t>(v)) + " (" + producer_kind_name(v) + ")";
}

std::string render(Outcome v)
{
  return std::to_string(static_cast<uint8_t>(v)) + " (" + outcome_name(v) + ")";
}

std::string render(ScreenShakeMode v)
{
  return std::to_string(static_cast<uint8_t>(v)) + " (" + screen_shake_mode_name(v) + ")";
}

std::string render(StartKind v)
{
  return std::to_string(static_cast<uint8_t>(v)) + " (" + start_kind_name(v) + ")";
}

std::string render(Bonus v)
{
  return std::to_string(static_cast<uint8_t>(v)) + " (" + bonus_name(v) + ")";
}

void push_field(std::vector<Difference>& out, DiffKind kind,
                const std::string& field, const std::string& expected,
                const std::string& actual)
{
  Difference d;
  d.kind = kind; d.field = field; d.expected = expected; d.actual = actual;
  out.push_back(d);
}

/** Like push_field, but for a difference meaningful only at a particular
    position -- a step for the inputs and visual tracks, a transition index
    for sector_transitions. Kept as a second, explicit function rather than
    an extra parameter on push_field so a call site's shape says whether
    has_step is set without reading the arguments. */
void push_step_field(std::vector<Difference>& out, DiffKind kind, uint32_t step,
                     const std::string& field, const std::string& expected,
                     const std::string& actual)
{
  Difference d;
  d.kind = kind; d.field = field; d.has_step = true; d.step = step;
  d.expected = expected; d.actual = actual;
  out.push_back(d);
}

/** Walks Header's tier-1 fields in declaration order
    (trace_header.hpp:72-127), skipping the tier-2 fields with a one-line
    comment at the point each is skipped so the exemptions are visible
    against the struct rather than inferred from absence. */
void compare_header(std::vector<Difference>& diffs,
                    const Header& expected, const Header& actual)
{
  // engine_version -- tier 2: a configure-time string, stale by construction
  // (BuildVersion.cmake runs configure_file once); it is a tier-3
  // precondition against this build instead.

  // platform -- tier 2: a replay runs on the machine doing the verifying, so
  // this field is a fact about *this* build on both sides of a round trip;
  // it is a tier-3 precondition instead.

  // level_path -- tier 2: an absolute local path that does not survive a
  // move between machines or checkouts. Level identity is level_md5, below.

  if (expected.level_md5 != actual.level_md5)
    push_field(diffs, DiffKind::HEADER, "level_md5",
              render(expected.level_md5), render(actual.level_md5));

  if (expected.seed != actual.seed)
    push_field(diffs, DiffKind::HEADER, "seed",
              render(expected.seed), render(actual.seed));

  if (!same_bits(expected.game_time_origin, actual.game_time_origin))
    push_field(diffs, DiffKind::HEADER, "game_time_origin",
              render(expected.game_time_origin), render(actual.game_time_origin));

  if (!same_bits(expected.step_rate, actual.step_rate))
    push_field(diffs, DiffKind::HEADER, "step_rate",
              render(expected.step_rate), render(actual.step_rate));

  if (expected.step_count != actual.step_count)
    push_field(diffs, DiffKind::HEADER, "step_count",
              render(expected.step_count), render(actual.step_count));

  if (!same_bits(expected.play_time, actual.play_time))
    push_field(diffs, DiffKind::HEADER, "play_time",
              render(expected.play_time), render(actual.play_time));

  if (expected.producer_kind != actual.producer_kind)
    push_field(diffs, DiffKind::HEADER, "producer_kind",
              render(expected.producer_kind), render(actual.producer_kind));

  // provenance -- tier 2: differs by construction on every correct replay --
  // it is what records that the run *was* a replay. The out-of-engine face
  // reads it for taint and loop findings instead; see the spec.

  if (expected.outcome != actual.outcome)
    push_field(diffs, DiffKind::HEADER, "outcome",
              render(expected.outcome), render(actual.outcome));

  if (expected.deaths != actual.deaths)
    push_field(diffs, DiffKind::HEADER, "deaths",
              render(expected.deaths), render(actual.deaths));

  if (expected.coins != actual.coins)
    push_field(diffs, DiffKind::HEADER, "coins",
              render(expected.coins), render(actual.coins));

  if (expected.viewport_width != actual.viewport_width)
    push_field(diffs, DiffKind::HEADER, "viewport_width",
              render(expected.viewport_width), render(actual.viewport_width));

  if (expected.viewport_height != actual.viewport_height)
    push_field(diffs, DiffKind::HEADER, "viewport_height",
              render(expected.viewport_height), render(actual.viewport_height));

  if (expected.screen_shake_mode != actual.screen_shake_mode)
    push_field(diffs, DiffKind::HEADER, "screen_shake_mode",
              render(expected.screen_shake_mode), render(actual.screen_shake_mode));

  if (!same_bits(expected.camera_peek_multiplier, actual.camera_peek_multiplier))
    push_field(diffs, DiffKind::HEADER, "camera_peek_multiplier",
              render(expected.camera_peek_multiplier), render(actual.camera_peek_multiplier));

  if (expected.start_kind != actual.start_kind)
    push_field(diffs, DiffKind::HEADER, "start_kind",
              render(expected.start_kind), render(actual.start_kind));
}

/** Walks the inputs track over the shared prefix of both tracks (the length
    mismatch itself, if any, was already reported by the caller as
    inputs.length) and reports only the first differing step -- a divergent
    replay differs at every subsequent step, and reporting them all would
    bury the one that matters. Both masks render by action name, never as
    raw bits, so a reader does not have to decode them by hand. */
void compare_inputs(std::vector<Difference>& diffs,
                    const InputTrack& expected, const InputTrack& actual)
{
  const uint32_t count = std::min(expected.step_count(), actual.step_count());
  for (uint32_t step = 0; step < count; ++step)
  {
    const ActionMask e_mask = expected.at(step);
    const ActionMask a_mask = actual.at(step);
    if (e_mask != a_mask)
    {
      push_step_field(diffs, DiffKind::INPUTS, step, "inputs",
                      describe_mask(e_mask), describe_mask(a_mask));
      break;
    }
  }
}

/** Walks the visual track the same way compare_inputs walks inputs -- shared
    prefix, first differing step only -- but reports *every* field that
    differs at that step rather than just the first, because a caller
    debugging a divergence wants to know whether it is one field going wrong
    or several (a single mis-sampled step tends to move more than one).

    action_id is resolved through each trace's own actions table before
    comparison; see the header comment on interned ids for why comparing the
    raw id would be wrong in both directions. StringTable::get() throws
    ParseError on an out-of-range id, and that is allowed to propagate here:
    validate() has already rejected such a trace, so a comparator that
    swallows the exception would report "equal" for a malformed one. */
void compare_visual(std::vector<Difference>& diffs, const Trace& expected,
                    const Trace& actual)
{
  const uint32_t count = std::min(expected.visual.step_count(),
                                  actual.visual.step_count());
  for (uint32_t step = 0; step < count; ++step)
  {
    const VisualFrame& ef = expected.visual.at(step);
    const VisualFrame& af = actual.visual.at(step);
    const size_t before = diffs.size();

    if (!same_bits(ef.pos_x, af.pos_x))
      push_step_field(diffs, DiffKind::VISUAL, step, "visual.pos_x",
                      render(ef.pos_x), render(af.pos_x));

    if (!same_bits(ef.pos_y, af.pos_y))
      push_step_field(diffs, DiffKind::VISUAL, step, "visual.pos_y",
                      render(ef.pos_y), render(af.pos_y));

    if (!same_bits(ef.vel_x, af.vel_x))
      push_step_field(diffs, DiffKind::VISUAL, step, "visual.vel_x",
                      render(ef.vel_x), render(af.vel_x));

    if (!same_bits(ef.vel_y, af.vel_y))
      push_step_field(diffs, DiffKind::VISUAL, step, "visual.vel_y",
                      render(ef.vel_y), render(af.vel_y));

    // action_id indexes each trace's own actions table (trace_visual.hpp:50);
    // two traces may intern the same strings under different ids, so only
    // the resolved name is comparable.
    const std::string& e_action = expected.actions.get(ef.action_id);
    const std::string& a_action = actual.actions.get(af.action_id);
    if (e_action != a_action)
      push_step_field(diffs, DiffKind::VISUAL, step, "visual.action",
                      e_action, a_action);

    if (ef.frame_idx != af.frame_idx)
      push_step_field(diffs, DiffKind::VISUAL, step, "visual.frame_idx",
                      render(static_cast<uint32_t>(ef.frame_idx)),
                      render(static_cast<uint32_t>(af.frame_idx)));

    if (ef.frame_progress != af.frame_progress)
      push_step_field(diffs, DiffKind::VISUAL, step, "visual.frame_progress",
                      render(static_cast<uint32_t>(ef.frame_progress)),
                      render(static_cast<uint32_t>(af.frame_progress)));

    if (ef.flags != af.flags)
      push_step_field(diffs, DiffKind::VISUAL, step, "visual.flags",
                      render(static_cast<uint32_t>(ef.flags)),
                      render(static_cast<uint32_t>(af.flags)));

    if (diffs.size() > before)
      break; // first differing step only, but every differing field at it
  }
}

/** Renders one transition as "<sector>@<first_step>", resolving sector_id
    through the table the transition's own trace carries -- the same
    resolve-before-compare rule as visual.action, for the same reason: two
    traces may intern the same sector names under different ids. */
std::string render_transition(const StringTable& sectors, const SectorTransition& t)
{
  return sectors.get(t.sector_id) + "@" + std::to_string(t.first_step);
}

/** Sector transitions compare like a track: length first (sector_transitions
    is not step-indexed the way inputs/visual are, so a count mismatch is
    "transitions.length" rather than "transitions" itself), then the shared
    prefix walked for the first differing entry. */
void compare_transitions(std::vector<Difference>& diffs, const Trace& expected,
                         const Trace& actual)
{
  if (expected.sector_transitions.size() != actual.sector_transitions.size())
    push_field(diffs, DiffKind::TRANSITIONS, "transitions.length",
              render(static_cast<uint32_t>(expected.sector_transitions.size())),
              render(static_cast<uint32_t>(actual.sector_transitions.size())));

  const size_t count = std::min(expected.sector_transitions.size(),
                                actual.sector_transitions.size());
  for (size_t i = 0; i < count; ++i)
  {
    const std::string e_rendered =
      render_transition(expected.sectors, expected.sector_transitions[i]);
    const std::string a_rendered =
      render_transition(actual.sectors, actual.sector_transitions[i]);
    if (e_rendered != a_rendered)
    {
      push_step_field(diffs, DiffKind::TRANSITIONS,
                      static_cast<uint32_t>(i), "transitions",
                      e_rendered, a_rendered);
      break;
    }
  }
}

/** Every StartState field, in declaration order (trace_start.hpp:51-89).
    Thirteen fields; a field added there without a line here is a field this
    comparator silently does not compare -- the count is asserted by the
    caller's test for exactly that reason. Unlike inputs/visual this is not a
    per-step track, so every differing field is reported, not just the
    first. */
void compare_start(std::vector<Difference>& diffs,
                   const StartState& expected, const StartState& actual)
{
  if (expected.sector != actual.sector)
    push_field(diffs, DiffKind::START, "start.sector",
              expected.sector, actual.sector);

  if (expected.spawnpoint != actual.spawnpoint)
    push_field(diffs, DiffKind::START, "start.spawnpoint",
              expected.spawnpoint, actual.spawnpoint);

  if (!same_bits(expected.position_x, actual.position_x))
    push_field(diffs, DiffKind::START, "start.position_x",
              render(expected.position_x), render(actual.position_x));

  if (!same_bits(expected.position_y, actual.position_y))
    push_field(diffs, DiffKind::START, "start.position_y",
              render(expected.position_y), render(actual.position_y));

  if (expected.is_checkpoint != actual.is_checkpoint)
    push_field(diffs, DiffKind::START, "start.is_checkpoint",
              render(expected.is_checkpoint), render(actual.is_checkpoint));

  if (!same_bits(expected.play_time, actual.play_time))
    push_field(diffs, DiffKind::START, "start.play_time",
              render(expected.play_time), render(actual.play_time));

  if (expected.coins != actual.coins)
    push_field(diffs, DiffKind::START, "start.coins",
              render(expected.coins), render(actual.coins));

  if (expected.tuxdolls != actual.tuxdolls)
    push_field(diffs, DiffKind::START, "start.tuxdolls",
              render(expected.tuxdolls), render(actual.tuxdolls));

  if (expected.bonus != actual.bonus)
    push_field(diffs, DiffKind::START, "start.bonus",
              render(expected.bonus), render(actual.bonus));

  if (expected.item_pocket != actual.item_pocket)
    push_field(diffs, DiffKind::START, "start.item_pocket",
              render(expected.item_pocket), render(actual.item_pocket));

  if (expected.coins_at_start != actual.coins_at_start)
    push_field(diffs, DiffKind::START, "start.coins_at_start",
              render(expected.coins_at_start), render(actual.coins_at_start));

  if (expected.bonus_at_start != actual.bonus_at_start)
    push_field(diffs, DiffKind::START, "start.bonus_at_start",
              render(expected.bonus_at_start), render(actual.bonus_at_start));

  if (expected.pocket_at_start != actual.pocket_at_start)
    push_field(diffs, DiffKind::START, "start.pocket_at_start",
              render(expected.pocket_at_start), render(actual.pocket_at_start));
}

} // namespace

std::string
describe_mask(ActionMask mask)
{
  if (mask == 0)
    return "-";

  std::string out;
  for (uint16_t i = 0; i < static_cast<uint16_t>(Action::COUNT); ++i)
  {
    const Action a = static_cast<Action>(i);
    if (mask & bit(a))
    {
      if (!out.empty())
        out += "|";
      out += action_name(a);
    }
  }

  // Reserved bits are rendered rather than dropped: InputTrack::push()
  // refuses them, so a valid trace never carries one, but a mask that
  // reaches this function any other way (a future caller building one by
  // hand, say) must not go silently missing from the rendered difference.
  const ActionMask reserved = mask & RESERVED_MASK;
  if (reserved != 0)
  {
    if (!out.empty())
      out += "|";
    std::ostringstream os;
    os << "reserved:0x" << std::hex << reserved;
    out += os.str();
  }

  return out;
}

const char*
kind_name(DiffKind kind)
{
  switch (kind)
  {
    case DiffKind::HEADER: return "header";
    case DiffKind::PRESENCE: return "presence";
    case DiffKind::INPUTS: return "inputs";
    case DiffKind::VISUAL: return "visual";
    case DiffKind::START: return "start";
    case DiffKind::TRANSITIONS: return "transitions";
  }
  return "?"; // guards a value outside the declared range -- reachable if this
              // is ever handed a byte decoded from a corrupt or forward-
              // incompatible file rather than one produced by this library
}

std::string
Difference::describe() const
{
  std::ostringstream os;
  // TRANSITIONS carries an index into sector_transitions, not a logical
  // step -- rendering it as "step N" would misname the number for the one
  // kind where it does not mean that. See the struct comment on `step`.
  if (has_step)
    os << (kind == DiffKind::TRANSITIONS ? "transition " : "step ") << step << " ";
  os << field << ": " << expected << " vs " << actual;
  return os.str();
}

std::vector<Difference>
compare(const Trace& expected, const Trace& actual)
{
  std::vector<Difference> diffs;

  // Presence is compared before content: a source carrying a track whose
  // replay carries none has diverged in the most consequential way
  // available, and reporting it as a length or content mismatch would bury
  // it. Returning early here would be wrong -- a caller wants every
  // difference, not just the first, so the header and track-length checks
  // below still run even when a presence flag disagrees.
  if (expected.has_visual != actual.has_visual)
    push_field(diffs, DiffKind::PRESENCE, "has_visual",
              render(expected.has_visual), render(actual.has_visual));

  if (expected.has_start != actual.has_start)
    push_field(diffs, DiffKind::PRESENCE, "has_start",
              render(expected.has_start), render(actual.has_start));

  compare_header(diffs, expected.header, actual.header);

  // inputs is the ground truth track and is always present regardless of
  // has_visual; its length is a fact distinct from header.step_count, which
  // is only the source's own declared count. Reported as "inputs.length"
  // rather than "step_count" again, so a header disagreement and a track
  // that is actually a different length are two facts, not one repeated.
  //
  // Length is reported before the step walk runs, and the walk itself still
  // runs over the shared prefix (min of the two lengths) regardless: a
  // truncated replay should report both its length and its first content
  // difference where one exists, not one or the other.
  if (expected.inputs.step_count() != actual.inputs.step_count())
    push_field(diffs, DiffKind::INPUTS, "inputs.length",
              render(expected.inputs.step_count()), render(actual.inputs.step_count()));

  compare_inputs(diffs, expected.inputs, actual.inputs);

  // visual, actions/sectors and sector_transitions all only mean something
  // when both sides claim to carry a visual track at all; a has_visual
  // mismatch above already reported the more consequential fact, and
  // validate() guarantees sector_transitions is empty on whichever side
  // lacks a visual track (trace_file.cpp:166-168), so comparing it further
  // would add nothing but a spurious "0 vs N".
  if (expected.has_visual && actual.has_visual)
  {
    if (expected.visual.step_count() != actual.visual.step_count())
      push_field(diffs, DiffKind::VISUAL, "visual.length",
                render(expected.visual.step_count()), render(actual.visual.step_count()));

    compare_visual(diffs, expected, actual);
    compare_transitions(diffs, expected, actual);
  }

  // Likewise the start chunk only means something when both sides carry
  // one; has_start's own mismatch above already reported the more
  // consequential fact, and a default-constructed StartState on the missing
  // side would otherwise report a field-by-field diff against defaults that
  // says nothing real.
  if (expected.has_start && actual.has_start)
    compare_start(diffs, expected.start, actual.start);

  return diffs;
}

} // namespace trace

/* EOF */
