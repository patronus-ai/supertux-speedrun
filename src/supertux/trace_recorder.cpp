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

#include "supertux/trace_recorder.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <memory>
#include <physfs.h>
#include <string>
#include <system_error>
#include <vector>

#include "addon/md5.hpp"
#include "supertux/globals.hpp"
#include "trace/trace_error.hpp"
#include "version.h"

namespace {

#if defined(__APPLE__)
constexpr const char* TRACE_PLATFORM = "darwin";
#elif defined(_WIN32)
constexpr const char* TRACE_PLATFORM = "windows";
#elif defined(__linux__)
constexpr const char* TRACE_PLATFORM = "linux";
#else
constexpr const char* TRACE_PLATFORM = "unknown";
#endif

} // namespace

namespace trace {

std::function<void(const Trace&)> TraceRecorder::s_finished_sink;

const char*
build_platform()
{
  return TRACE_PLATFORM;
}

std::array<uint8_t, 16>
level_md5(const std::string& physfs_path)
{
  std::array<uint8_t, 16> digest = {};

  // PHYSFS_openRead() dereferences PhysFS's internal state without a null
  // check and crashes if PHYSFS_init() was never called, rather than
  // failing gracefully -- confirmed against libphysfs 1.x on this tree. A
  // caller that has not initialized PhysFS (e.g. a unit test exercising
  // make_header() without engine startup) hits that path, so guard it here
  // rather than let it segfault: an uninitialized PhysFS is just another
  // reason the level is unreadable, and falls under the same all-zero
  // "unknown" digest as a missing file.
  if (!PHYSFS_isInit())
    return digest;

  PHYSFS_File* file = PHYSFS_openRead(physfs_path.c_str());
  if (file == nullptr)
    return digest;

  MD5 md5;
  while (true)
  {
    unsigned char buffer[1024];
    const PHYSFS_sint64 len = PHYSFS_readBytes(file, buffer, sizeof(buffer));
    if (len <= 0)
      break;

    md5.update(buffer, static_cast<unsigned int>(len));
  }
  PHYSFS_close(file);

  // MD5::raw_digest() heap-allocates its 16-byte buffer and transfers
  // ownership to the caller (src/addon/md5.cpp:146-153); unlike
  // hex_digest(), it does not free it internally. Wrap it immediately so
  // the transfer is self-documenting and the buffer isn't leaked.
  const std::unique_ptr<uint8_t[]> raw(md5.raw_digest());
  for (size_t i = 0; i < digest.size(); ++i)
    digest[i] = raw[i];

  return digest;
}

// The trace format declares its own ScreenShakeMode rather than serializing
// Config::ScreenShakeMode by value, for the same reason trace_control_map.hpp
// does not serialize Control by value: upstream may reorder the engine enum.
// These asserts fail the build if it does, forcing an audit of the map instead
// SCREEN SHAKE: this engine has no setting for it.
//
// The fork this came from has Config::ScreenShakeMode (OFF/REDUCED/FULL),
// an accessibility setting that gates the camera earthquake, and it is a
// replay precondition there because it changes what the camera does -- and
// the camera is gameplay. SuperTux 0.6.3 has no such setting: the shake
// always runs. So the two conversion helpers have nothing to convert and
// are omitted rather than stubbed.
//
// RecorderConfig::screen_shake_mode therefore keeps its FULL default and no
// caller sets it, which is the truthful value for this engine. A trace
// recorded here says FULL and means it.

// Same contract as the ScreenShakeMode asserts above: the trace format
// declares its own Bonus rather than serializing BonusType by value, so a
// reorder upstream must fail the build here instead of writing a different
// bonus into every start chunk.
// 0.6.3 spells these NO_BONUS/GROWUP_BONUS/...; the fork this came from
// spells them BONUS_NONE/BONUS_GROWUP/.... Same values, renamed upstream
// after 0.6.3, so only the identifiers differ and the asserts still pin
// what they always pinned.
static_assert(static_cast<int>(NO_BONUS) == 0, "");
static_assert(static_cast<int>(GROWUP_BONUS) == 1, "");
static_assert(static_cast<int>(FIRE_BONUS) == 2, "");
static_assert(static_cast<int>(ICE_BONUS) == 3, "");
static_assert(static_cast<int>(AIR_BONUS) == 4, "");
static_assert(static_cast<int>(EARTH_BONUS) == 5, "");

Bonus
trace_bonus(BonusType bonus)
{
  switch (bonus)
  {
    case NO_BONUS: return Bonus::NONE;
    case GROWUP_BONUS: return Bonus::GROWUP;
    case FIRE_BONUS: return Bonus::FIRE;
    case ICE_BONUS: return Bonus::ICE;
    case AIR_BONUS: return Bonus::AIR;
    case EARTH_BONUS: return Bonus::EARTH;
  }
  // Load-bearing, for the same reason the throw in trace_screen_shake_mode()
  // is: the static_asserts above pin the values of the six enumerators that
  // exist today and cannot detect a seventh being added, and WARNINGS/WERROR
  // both default OFF (mk/cmake/SuperTux/WarningFlags.cmake) so an unhandled
  // enumerator fails nothing at build time. The caller must not let this
  // escape uncaught.
  throw ParseError("unknown BonusType");
}

BonusType
engine_bonus(Bonus bonus)
{
  switch (bonus)
  {
    case Bonus::NONE: return NO_BONUS;
    case Bonus::GROWUP: return GROWUP_BONUS;
    case Bonus::FIRE: return FIRE_BONUS;
    case Bonus::ICE: return ICE_BONUS;
    case Bonus::AIR: return AIR_BONUS;
    case Bonus::EARTH: return EARTH_BONUS;
  }
  // Load-bearing for the same reason trace_bonus()'s throw is: the
  // static_asserts above pin the values of the enumerators that exist today
  // and cannot detect a seventh being added, and WARNINGS/WERROR both
  // default OFF (mk/cmake/SuperTux/WarningFlags.cmake) so an unhandled
  // enumerator fails nothing at build time.
  throw ParseError("unknown trace::Bonus");
}

Header
make_header(const RecorderConfig& config)
{
  Header header;
  header.engine_version = PACKAGE_VERSION;
  header.platform = TRACE_PLATFORM;
  header.level_path = config.level_path;
  header.level_md5 = level_md5(config.level_path);
  header.seed = config.seed;
  // game_time_origin is deliberately left at 0: the recorder derives it from
  // the clock at its first recorded step, not here. See RecorderConfig.
  header.step_rate = config.step_rate;
  header.producer_kind = ProducerKind::HUMAN;
  header.provenance = config.provenance;
  header.viewport_width = config.viewport_width;
  header.viewport_height = config.viewport_height;
  header.screen_shake_mode = config.screen_shake_mode;
  header.camera_peek_multiplier = config.camera_peek_multiplier;
  return header;
}

TraceRecorder::TraceRecorder(const RecorderConfig& config) :
  m_trace(),
  m_output_path(config.output_path),
  m_expected_dt(config.step_rate > 0.0f ? 1.0f / config.step_rate : 0.0f),
  m_step_dt(0.0f),
  m_pending_mask(0),
  m_invalid_reason(),
  m_awaiting_visual(false),
  m_finished(false),
  m_has_game_time_origin(false),
  m_has_start_state(false),
  m_start_unknown_reason(),
  m_previous_was_dead(false),
  m_has_previous_sector(false),
  m_previous_sector_id(0)
{
  m_trace.header = make_header(config);
}

void
TraceRecorder::note_step(float dt_sec, float speed_multiplier)
{
  // Stashed before the latch, and before any validity judgement: this is the
  // only place the step's actual length is in hand, and record_visual() needs
  // it to record the clock BEFORE the step even when the capture is already
  // marked invalid.
  m_step_dt = dt_sec;

  if (!m_invalid_reason.empty())
    return;

  if (speed_multiplier != 1.0f)
  {
    m_invalid_reason =
      "recorded with a debug speed multiplier of " +
      std::to_string(speed_multiplier) + ": this capture is not comparable";
    return;
  }

  // Relative comparison: the nominal step is ~0.015 s, so an absolute epsilon
  // would be either too loose at 66.7 Hz or too tight at a different rate.
  if (m_expected_dt <= 0.0f ||
      std::fabs(dt_sec - m_expected_dt) > m_expected_dt * 0.001f)
  {
    m_invalid_reason =
      "unexpected step length: expected " + std::to_string(m_expected_dt) +
      " s, saw " + std::to_string(dt_sec) + " s";
  }
}

void
TraceRecorder::note_environment(uint32_t viewport_width,
                                uint32_t viewport_height,
                                ScreenShakeMode screen_shake_mode,
                                float camera_peek_multiplier)
{
  if (!m_invalid_reason.empty())
    return;

  const Header& header = m_trace.header;

  if (viewport_width != header.viewport_width ||
      viewport_height != header.viewport_height)
  {
    m_invalid_reason =
      "viewport changed mid-capture: header says " +
      std::to_string(header.viewport_width) + "x" +
      std::to_string(header.viewport_height) + ", saw " +
      std::to_string(viewport_width) + "x" + std::to_string(viewport_height);
    return;
  }

  if (screen_shake_mode != header.screen_shake_mode)
  {
    m_invalid_reason =
      "screen shake mode changed mid-capture: header says " +
      std::to_string(static_cast<int>(header.screen_shake_mode)) + ", saw " +
      std::to_string(static_cast<int>(screen_shake_mode));
    return;
  }

  // Exact comparison, deliberately: this is the same float that was copied
  // from the same config field, so a tolerance would only hide a real change
  // to a value the user set.
  if (camera_peek_multiplier != header.camera_peek_multiplier)
  {
    m_invalid_reason =
      "camera peek multiplier changed mid-capture: header says " +
      std::to_string(header.camera_peek_multiplier) + ", saw " +
      std::to_string(camera_peek_multiplier);
  }
}

void
TraceRecorder::record_inputs(ActionMask mask)
{
  if (m_awaiting_visual)
    throw ParseError("record_inputs called twice for one step");

  // The mask is validated here so a bad one fails at the call site that
  // produced it, but it is not pushed until record_visual() completes the
  // step. Buffering makes an unpaired input structurally impossible: if a run
  // ends between the two calls, this step simply never lands, instead of
  // leaving inputs one ahead of visual and failing validate() at write time.
  if ((mask & RESERVED_MASK) != 0)
    throw ParseError("action mask sets a reserved bit");

  m_pending_mask = mask;
  m_awaiting_visual = true;
}

void
TraceRecorder::record_visual(const PlayerSnapshot& snapshot,
                             const std::string& sector_name)
{
  if (!m_awaiting_visual)
    throw ParseError("record_visual called without a pending step");

  // Everything that can throw happens before anything is committed, so a
  // failure here leaves the recorder exactly as it was and the caller can
  // retry the step. Both intern() calls throw when a string table fills, and
  // if either fired after the input had already landed, inputs would sit one
  // step ahead of visual -- the desync the buffering above exists to prevent.
  const uint16_t sector_id = m_trace.sectors.intern(sector_name);
  const VisualFrame frame = to_visual_frame(snapshot, m_trace.actions);

  // Commit phase: nothing below can throw on any reachable path (a
  // sector_transitions push_back could in principle raise bad_alloc, but an
  // allocation failure mid-recording is not a case this class can
  // meaningfully recover from, and every other container here shares that
  // property).
  const uint32_t step = m_trace.visual.step_count();

  // The origin is fixed by the first step that LANDS, not at construction and
  // not when a step merely opens: a GameSession is built before its level
  // intro runs (an unbounded wait, during which g_game_time keeps
  // accumulating), and a step that opens can still be cancelled. Here is the
  // only point at which "this is step 0 of the trace as it will be written" is
  // true. reset_tracks() clears the flag, so a reloaded level re-derives the
  // origin for the attempt the trace ends up holding.
  //
  // The stored value is the clock BEFORE the step, not the observed clock:
  // screen_manager.cpp:681 adds the step's dtime to g_game_time before
  // update_gamelogic() runs it, so everything inside a step observes a clock
  // that already includes it. Subtracting note_step()'s dt makes the field a
  // value the executor assigns directly. g_game_time does not change between
  // note_step() and here (only screen_manager's step loop writes it), so this
  // is exactly the clock note_step() would have computed.
  if (!m_has_game_time_origin)
  {
    m_trace.header.game_time_origin = g_game_time - m_step_dt;
    m_has_game_time_origin = true;
  }

  if (!m_has_previous_sector || sector_id != m_previous_sector_id)
  {
    SectorTransition transition;
    transition.sector_id = sector_id;
    transition.first_step = step;
    m_trace.sector_transitions.push_back(transition);

    m_has_previous_sector = true;
    m_previous_sector_id = sector_id;
  }

  if (snapshot.dead && !m_previous_was_dead)
    ++m_trace.header.deaths;

  m_previous_was_dead = snapshot.dead;

  m_trace.inputs.push(m_pending_mask);
  m_trace.visual.push(frame);
  m_trace.has_visual = true;
  m_awaiting_visual = false;
}

void
TraceRecorder::cancel_step()
{
  // No guard on m_awaiting_visual: an unconditional clear is idempotent, and a
  // "cancel with nothing open" is exactly what the caller's own early returns
  // produce when they bail out before opening a step.
  m_pending_mask = 0;
  m_awaiting_visual = false;
}

void
TraceRecorder::reset_tracks() noexcept
{
  // noexcept is contingent on InputTrack, VisualTrack and StartState having
  // non-allocating default constructors, which they do today. A future member
  // that allocates in its default constructor would turn a recording fault into
  // std::terminate, breaking "a recording fault must never take down a live
  // game session" -- drop the noexcept if that changes.
  //
  // Move-assigning fresh containers rather than adding clear() to InputTrack
  // and VisualTrack: src/trace is the format library shared with the Python
  // reference, and this needs no new API there.
  m_trace.inputs = InputTrack();
  m_trace.visual = VisualTrack();
  m_trace.has_visual = false;
  m_trace.sector_transitions.clear();

  // The new attempt's step 0 must open its own sector transition -- validate()
  // requires a transition at step 0 -- so the previous-sector state cannot
  // survive, even when the respawn lands in the same-named sector.
  m_has_previous_sector = false;
  m_previous_sector_id = 0;

  // A step opened before the reload belongs to the attempt being discarded.
  cancel_step();

  // The origin describes the attempt the trace holds, so it is re-sampled by
  // the first step of the new attempt.
  m_has_game_time_origin = false;
  m_trace.header.game_time_origin = 0.0f;

  // A new attempt has a new start state: it may respawn at a checkpoint the
  // previous attempt never reached, and it carries the coins that attempt
  // banked. Keeping the previous attempt's answer would describe the trace's
  // step 0 as somewhere it never was -- the exact failure the start chunk
  // exists to remove. The chunk itself goes back to its defaults alongside the
  // flag, so an attempt that ends before its first step cannot leave the
  // previous attempt's chunk behind (validate() rejects a chunk under an
  // UNKNOWN kind, which would lose the trace at write time).
  m_has_start_state = false;
  m_start_unknown_reason.clear();
  m_trace.has_start = false;
  m_trace.start = StartState();
  m_trace.header.start_kind = StartKind::UNKNOWN;

  // Deliberately NOT reset:
  //   header.deaths -- the death counter spans the whole session (a trace
  //     reads "deaths=3, and here is the final attempt").
  //   m_previous_was_dead -- kept so the edge detector spans the session too.
  //     This is defensive, not load-bearing: restart_level() respawns Tux
  //     alive, so no reachable path has been found where clearing it changes
  //     the count, and no test pins it (deaths_keep_counting_across_a_reset
  //     passes either way, because the post-reset sequence is alive->dead
  //     under both). Treat it as belt-and-braces, not as a fix for a bug.
  //   m_invalid_reason -- one bad step taints the capture; latching is
  //     structural (nothing clears it) and a reload is not absolution.
  //   m_step_dt -- the dt stashed by note_step() for the step now in flight.
  //     Safe to keep because note_step() and record_visual() are adjacent in
  //     GameSession::update(), so the stash is always current; listed here so
  //     the ledger stays exhaustive.
  //   m_trace.sectors / m_trace.actions -- interned ids stay stable; the cost
  //     is at most a few table entries the surviving tracks no longer
  //     reference, which validate() permits.
  //   m_finished, m_output_path, m_expected_dt and the rest of the header --
  //     session-level, not attempt-level.
}

void
TraceRecorder::record_start_state(const StartState& state, StartKind kind)
{
  // UNKNOWN is refused rather than accepted-and-stored: it is the one kind
  // that forbids a start chunk, so writing one under it would produce a trace
  // serialize() rejects. A caller that cannot describe the start has
  // mark_start_unknown() to say so.
  if (kind == StartKind::UNKNOWN)
    throw ParseError("record_start_state called with an UNKNOWN kind");

  m_trace.header.start_kind = kind;
  m_trace.start = state;
  m_trace.has_start = true;
  m_start_unknown_reason.clear();
  m_has_start_state = true;
}

void
TraceRecorder::mark_start_unknown(const std::string& reason)
{
  // The chunk is dropped, not merely left alone: a start state recorded for an
  // earlier attempt would otherwise sit under an UNKNOWN kind, which
  // validate() rejects -- losing the whole trace at write time -- and which
  // would describe a start this attempt never had.
  m_trace.header.start_kind = StartKind::UNKNOWN;
  m_trace.has_start = false;
  m_trace.start = StartState();
  m_start_unknown_reason = reason;
  m_has_start_state = true;
}

void
TraceRecorder::finish(Outcome outcome, float play_time, uint32_t coins)
{
  if (m_finished)
    throw ParseError("finish called twice");

  m_trace.header.outcome = outcome;
  m_trace.header.step_count = m_trace.inputs.step_count();
  m_trace.header.play_time = play_time;
  m_trace.header.coins = coins;

  // Nothing consults is_valid() at write time, and a slow-motion capture would
  // otherwise serialize looking exactly like a clean one. The notes chunk is
  // opaque to the library by design, which makes it the right carrier: this is
  // the only place the invalidity survives into the file itself. The same
  // holds for the reason a start state could not be captured: header.start_kind
  // says an executor must refuse the trace, but only the note says why.
  //
  // Both keys are optional and independent: a capture can be invalid with a
  // known start, or valid with an unknown one.
  //
  // Neither reason is escaped for JSON. Both are engine-generated, not
  // user-supplied, and no reason built today contains a quote or a backslash --
  // but m_invalid_reason already interpolates std::to_string of a float, so a
  // future reason that interpolates something less tame would produce
  // malformed JSON here. Escape at that point rather than assuming.
  //
  // An attempt that ended before its first step never reached the capture, so
  // no reason was ever set -- but the trace still says start_kind = UNKNOWN,
  // which an executor must refuse. Naming it here keeps "UNKNOWN is always
  // explained" true of every trace this class writes, rather than leaving one
  // case where the file records a refusal and not its cause.
  //
  // Conditioned on both facts, not just the kind: a caller that records steps
  // without ever supplying a start state is a caller error, not an ended
  // attempt, and inventing this reason for it would be a wrong explanation
  // rather than a missing one.
  if (m_start_unknown_reason.empty() && !m_has_start_state &&
      m_trace.header.step_count == 0)
  {
    m_start_unknown_reason =
      "the attempt ended before its first recorded step, so where it began "
      "was never captured";
  }

  std::vector<std::string> fields;
  if (!m_invalid_reason.empty())
    fields.push_back("\"invalid\":\"" + m_invalid_reason + "\"");
  if (!m_start_unknown_reason.empty())
    fields.push_back("\"start_unknown\":\"" + m_start_unknown_reason + "\"");

  if (!fields.empty())
  {
    std::string note = "{";
    for (size_t i = 0; i < fields.size(); ++i)
    {
      if (i > 0)
        note += ",";
      note += fields[i];
    }
    note += "}";
    m_trace.notes.set_episode(std::vector<uint8_t>(note.begin(), note.end()));
  }

  m_finished = true;
}

void
TraceRecorder::set_finished_sink(std::function<void(const Trace&)> sink)
{
  s_finished_sink = std::move(sink);
}

void
TraceRecorder::write() const
{
  // serialize() validates first, so an internally inconsistent trace throws
  // before anything touches the filesystem.
  const std::vector<uint8_t> bytes = serialize(m_trace);

  // The finished-trace sink fires HERE, and the position is the whole of what
  // makes it a gate rather than a notification: serialize() is what calls
  // validate(), so a trace that could not have been written has already
  // thrown by this line and the sink never fires for it. A verifier therefore
  // has nothing to compare and reports "no replay" (exit 2) instead of
  // possibly reporting a match for a trace no consumer could ever read.
  //
  // It was in finish() first, which did NOT achieve that: GameSession's
  // destructor calls finish() and then write(), and it catches the ParseError
  // write() throws (a destructor must not throw), so the invalid trace had
  // already reached the verifier by the time anything objected. The ordering
  // only works from inside write(), after this line.
  //
  // Also strictly after finish() by construction -- write() on an unfinished
  // recorder would serialize a header with no step_count or outcome -- so the
  // sink still sees every closing field and the episode note in place.
  //
  // What the gate is, precisely, so it is not over-read: it is VALIDATION,
  // not the write landing. Under --verify-trace composed with
  // --record-trace, the sink fires above the filesystem work below, so a
  // disk-full failure still delivers the trace and the comparison still
  // happens -- which is right. "This trace is internally consistent" is what
  // a comparison needs; "this trace reached the disk" is a separate concern
  // the caller hears about through the ParseError.
  //
  // s_finished_sink is static, which is why a const method can call it; see
  // set_finished_sink() for why the channel is a static at all. Guarded on it
  // being installed: the null test is what makes an ordinary recording pay
  // nothing for this.
  if (s_finished_sink)
    s_finished_sink(m_trace);

  // An empty output_path means "do not write": --verify-trace without
  // --record-trace compares the finished trace in memory and needs no
  // writable directory anywhere (see the spec's "The engine face:
  // --verify-trace FILE"). The serialized bytes above are then discarded,
  // which is the only cost, and it buys the validation gate described there.
  if (m_output_path.empty())
    return;

  // Written to a sibling ".part" and renamed into place, so the target path
  // only ever holds a complete trace. Truncating the target directly would
  // mean a disk-full failure mid-write leaves a truncated .stgt at exactly the
  // path a consumer reads, with nothing but a log line to say so -- and a
  // crash or SIGKILL between truncation and the last byte leaves the same
  // thing. The partial is removed on every failure path, so a failed run
  // leaves no debris either.
  const std::string partial = m_output_path + ".part";

  {
    std::ofstream out(partial, std::ios::binary);
    if (!out)
      throw ParseError("cannot open trace output file: " + partial);

    out.write(reinterpret_cast<const char*>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    // close() explicitly rather than at scope exit: a full disk is only
    // reported when the last buffer is flushed, and the destructor would
    // swallow that.
    out.close();
    if (!out)
    {
      std::error_code ignored;
      std::filesystem::remove(partial, ignored);
      throw ParseError("failed writing trace to " + partial);
    }
  }

  // std::filesystem::rename, not std::rename: the standard requires it to
  // REPLACE an existing target file, while std::rename fails on Windows when
  // the target exists -- which is every re-recording to the same path.
  std::error_code ec;
  std::filesystem::rename(partial, m_output_path, ec);
  if (ec)
  {
    std::error_code ignored;
    std::filesystem::remove(partial, ignored);
    throw ParseError("failed to move the finished trace into place at " +
                     m_output_path + ": " + ec.message());
  }
}

} // namespace trace

/* EOF */
