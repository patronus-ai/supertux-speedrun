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
#include <functional>
#include <string>

#include "supertux/gameconfig.hpp"
#include "supertux/player_status.hpp"
#include "supertux/trace_sampler.hpp"
#include "trace/trace_file.hpp"
#include "trace/trace_header.hpp"
#include "trace/trace_start.hpp"

namespace trace {

/** MD5 of a level file, read through PhysFS.

    Uses a raw PHYSFS_openRead/PHYSFS_readBytes loop rather than IFileStream:
    src/addon/addon_manager.cpp:46 carries a standing TODO that IFileStream
    "does not work as expected for some files", and it reads add-on files the
    same way for that reason.

    Returns an all-zero digest when the file cannot be opened, or when PhysFS
    has not been initialized yet (PHYSFS_openRead() is unsafe to call before
    PHYSFS_init(), so this is checked explicitly rather than left to crash) --
    a trace of an unreadable level is still worth having, and the zero digest
    is a recognizable "unknown" that a later phase can warn about. */
std::array<uint8_t, 16> level_md5(const std::string& physfs_path);

/** The platform string this build records into header.platform -- "darwin",
    "windows", "linux" or "unknown".

    Exposed so that a caller checking a trace's recorded platform against this
    build (TraceExecutor::platform_matches()) reads the *same* value the
    recorder wrote, rather than repeating the preprocessor chain that derives
    it. Two derivations that could disagree would make the check report drift
    that does not exist, which is the failure mode the field's single hasher
    already avoids for level_md5. */
const char* build_platform();

/** Everything the recorder needs that it cannot discover for itself.

    There is deliberately no game_time_origin here: a GameSession is
    constructed long before its first recorded step (setup() pushes LevelIntro,
    which waits on a keypress with no timeout, while g_game_time keeps
    accumulating), so a value sampled at construction under-reports the origin
    of step 0 by however long the player spent on the intro. The recorder
    derives it itself, from the clock at the first step that actually lands --
    see record_visual(). */
struct RecorderConfig final
{
  /** Real-filesystem path, not a PhysFS path: an agent harness needs to choose
      exactly where the output lands, and PhysFS write-dir semantics would
      confine it to the userdir -- a separate concern from --userdir isolation,
      which is about keeping ambient config and profiles out of a run.

      *Empty means "do not write"*, and is not an error: --verify-trace runs
      the recorder for its in-memory trace() alone and needs no writable
      directory at all (see the spec's "The engine face: --verify-trace
      FILE"). write() still serializes in that case -- see write(). */
  std::string output_path;
  std::string level_path;
  int32_t seed = 0;
  float step_rate = 0.0f;
  std::string provenance;

  /** The viewport at session construction. note_environment() re-checks it
      every step and invalidates the capture on a change, which is what makes
      it trustworthy across the whole recording; sampled here because
      trace::validate() rejects a zero viewport and serialize() calls
      validate(), so a capture invalidated before its first step must still
      have a writable header -- "an invalid capture is written and marked"
      (see the spec's header-chunk section). */
  uint32_t viewport_width = 0;
  uint32_t viewport_height = 0;
  ScreenShakeMode screen_shake_mode = ScreenShakeMode::FULL;
  float camera_peek_multiplier = 1.0f;
};

/** Builds the header a recording starts with. step_count, play_time, outcome,
    deaths and coins are filled in when recording stops; game_time_origin is
    filled in by the first recorded step and is 0 here. */
Header make_header(const RecorderConfig& config);

/* The screen-shake conversions the fork this came from declares here are
   absent: SuperTux 0.6.3 has no screen-shake setting. See the note in
   trace_recorder.cpp. */

/** Maps the engine's BonusType onto the trace format's own Bonus. Same
    reasoning as trace_screen_shake_mode() above: the format declares its own
    enum rather than serializing BonusType by value, so this mapping and the
    static_asserts pinning it in trace_recorder.cpp are what keep the two in
    agreement. Throws ParseError if given a BonusType outside the six known
    values, which is only reachable through a cast. */
Bonus trace_bonus(BonusType bonus);

/** Maps the trace format's Bonus back onto the engine's BonusType -- the
    inverse of trace_bonus(), and what the `start` chunk's restore half
    needs. The static_asserts in trace_recorder.cpp pin both directions
    against the same six enumerator values, so an upstream reorder fails the
    build here rather than restoring a different bonus than was recorded.

    Throws ParseError if given a Bonus outside the six known values, which
    is only reachable through a cast. */
BonusType engine_bonus(Bonus bonus);

/** Accumulates a played run into a Trace, one logical step at a time.

    A step is exactly one record_inputs() followed by one record_visual().
    Calling them out of order is a programming error, not malformed input, but it
    throws rather than silently producing a trace whose visual track is offset
    from its inputs by a step -- a defect that would round-trip cleanly and only
    show up as a ghost that lags by 15 ms.

    record_inputs() only buffers its mask; both tracks are pushed together in
    record_visual(). That makes an unpaired input structurally impossible: if a
    run ends between the two calls, the incomplete step simply never lands,
    instead of leaving `inputs` one step ahead of `visual` and failing
    validate() only later, at write time. */
class TraceRecorder final
{
public:
  explicit TraceRecorder(const RecorderConfig& config);

  /** Called once per logical step, before the simulation advances. Throws
      ParseError if a reserved mask bit is set, or if a step is already
      awaiting its record_visual(). */
  void record_inputs(ActionMask mask);

  /** Called once per logical step, after the simulation advances. Pushes both
      the buffered input mask and this visual frame together, so a step can
      never land in one track without the other -- see note_step() and the
      class comment.

      The first step to land here also fills in header.game_time_origin, as
      `g_game_time` minus the dt note_step() saw for this same step. That
      subtraction is not cosmetic: screen_manager.cpp:681 does
      `g_game_time += dtime` *before* update_gamelogic(dtime), so the clock
      observed anywhere inside a step already includes that step's dtime.
      Storing the observed value would make the header hold the clock *after*
      step 0, and an executor that assigns it and then runs the same
      increment-first loop would be one step ahead of the recording.

      So the field is the clock as it stood *before* step 0 -- a value the
      executor assigns directly, with no correction to remember. See the
      spec's "Seeding and clock restoration" section. */
  void record_visual(const PlayerSnapshot& snapshot, const std::string& sector_name);

  /** Abandons a step that record_inputs() opened but that will never get its
      visual sample, leaving the recorder ready for the next step.

      Nothing is committed until record_visual(), so an abandoned step costs
      only the buffered mask. Without this, a caller that opens a step and then
      bails out before the visual sample leaves the recorder awaiting a visual
      forever: the next record_inputs() throws, and a caller that treats a
      recorder throw as "stop recording" loses every step recorded so far.

      Idempotent: calling it with no step open does nothing. */
  void cancel_step();

  /** Drops everything recorded so far and starts the step numbering over,
      keeping the session-level header fields.

      Called when the engine reloads the level under the recorder
      (GameSession::restart_level, which SuperTux runs on every death). The
      spec's "Reset alignment" clause requires it: without it a trace splices
      several attempts, so step_count no longer maps to any engine clock, a
      respawn into the same-named sector is an unannotated teleport in the
      visual track, and replaying the input track in a fresh process cannot
      reproduce the trace.

      Cleared: both tracks, has_visual, the sector transitions and the
      previous-sector state (so the new attempt's step 0 opens its own
      transition, which validate() requires), any step opened but not yet
      committed, the sampled game_time_origin -- the origin must describe
      the attempt the trace actually holds -- and the start state, chunk and
      kind together, because the new attempt begins somewhere else and must
      record its own.

      Kept: everything that describes the session rather than the attempt --
      header.deaths and the previous-dead edge state that feeds it (so the
      death that triggered this reload is not counted twice), the latched
      invalid_reason (one bad step taints the capture), the interned string
      tables (ids stay stable, at the cost of a few unreferenced entries), and
      the finished flag.

      Does not throw: every operation here is a clear or a move-assignment. */
  void reset_tracks() noexcept;

  /** True until this attempt's start state has been recorded. Cleared by
      record_start_state() and mark_start_unknown(), set again by
      reset_tracks() -- each attempt owns its own start state. */
  inline bool needs_start_state() const { return !m_has_start_state; }

  /** Records where this attempt began. See the spec's `start` chunk section
      for what the record must contain and why. */
  void record_start_state(const StartState& state, StartKind kind);

  /** Records that the start state could NOT be captured: sets
      header.start_kind to UNKNOWN, drops any start chunk, and carries the
      reason into the episode note at finish().

      This is not an invalid capture. The recorded steps stay comparable; what
      is lost is the ability to execute them, which an executor must refuse on
      seeing UNKNOWN. */
  void mark_start_unknown(const std::string& reason);

  /** The capture validity guard, called once per logical step before the
      simulation advances.

      This is also where the actual step length is in hand, so it stashes
      dt_sec for record_visual() to turn g_game_time into the clock *before*
      the step. Stashed unconditionally, ahead of the latch below, so an
      already-invalid capture still gets a truthful origin.

      speed_multiplier is g_debug.get_game_speed_multiplier(). It is the debug
      knob, and the only speed input that makes a capture incomparable --
      the engine also scales dt via m_speed for the end sequence and pause
      (screen_manager.cpp:680, and its comment there), but those are legitimate
      gameplay and the call site excludes them. dt_sec is still checked as an
      independent sanity guard against an unexpected step length.

      Once set, invalid_reason() latches: a later good step cannot clear it,
      because one bad step taints the whole capture. The first cause is the
      one retained -- a later, different failure does not overwrite it, so the
      reported reason is always the root cause rather than a derived
      symptom. */
  void note_step(float dt_sec, float speed_multiplier);

  /** Called once per step, from the same place as note_step().

      The environment is sampled into the header at construction, not here,
      because trace::validate() rejects a zero viewport and serialize() calls
      validate() -- so a capture invalidated before its first step must still
      have a writable header, which is P2's "an invalid capture is written and
      marked" property.

      What this does is compare: all four values reach gameplay through the
      camera (audit H4), and all four are changeable without leaving the level
      -- a window resize, or the in-game options menu. A change part-way
      through means the header describes only part of the trace, so the
      capture is marked invalid. Latches like note_step(): the first cause is
      the one retained. */
  void note_environment(uint32_t viewport_width, uint32_t viewport_height,
                        ScreenShakeMode screen_shake_mode,
                        float camera_peek_multiplier);

  /** Throws ParseError if called more than once.

      Does NOT invoke the finished-trace sink -- write() does, once
      serialize() has accepted the trace. It was here first, and moving it was
      the point: a sink fired from this function delivers a trace before
      anything has validated it, which is not what a verifier can be given.
      See set_finished_sink(). */
  void finish(Outcome outcome, float play_time, uint32_t coins);

  /** Installed by main() under --verify-trace, invoked from write() once
      serialize() has accepted the trace. The comparison needs the trace the
      recorder finished with, and it needs it without a file:
      TraceRecorder::trace() already exposes it, but the recorder is built
      inside GameSession's constructor and destroyed inside its destructor, so
      main never holds one to ask.

      *Fired from write() rather than finish(), and that is a gate rather than
      a detail of placement.* serialize() calls validate(), so a trace that
      could never have been written has thrown before the sink is reached and
      a verifier is handed nothing at all -- which it reports as "no replay"
      rather than as a possible match. Firing from finish() did not achieve
      that: GameSession's destructor calls finish() and then write(), and it
      catches write()'s ParseError because a destructor must not throw, so the
      invalid trace reached the verifier first and the objection arrived
      afterwards as a log line.

      A process-global static is therefore the channel, and that is a cost to
      state rather than a convenience to enjoy -- Editor::s_resaving_in_progress
      (editor.hpp) is the in-tree precedent for process-level state of this
      shape. Null by default, so an ordinary recording pays nothing: no
      allocation, no call, one null test per session.

      Covered by TraceRecorderTest, which already links this file against
      PhysFS: one test pins that the delivered trace carries the closing
      header fields AND the episode note, and one pins that a trace failing
      validate() never reaches the sink at all. Both fail under the
      corresponding misplacement, which is what makes them coverage rather
      than description.

      Never cleared by the recorder, but main() clears it in ~Main -- a
      process-lifetime static must not outlive a captured `this`. main()
      installs it once per process, before any session exists, and a
      --verify-trace run starts exactly one session; the sink itself decides
      what to do if that ever stops being true. */
  static void set_finished_sink(std::function<void(const Trace&)> sink);

  inline bool is_valid() const { return m_invalid_reason.empty(); }
  inline const std::string& invalid_reason() const { return m_invalid_reason; }
  inline uint32_t step_count() const { return m_trace.inputs.step_count(); }
  inline const Trace& trace() const { return m_trace; }
  inline const std::string& output_path() const { return m_output_path; }

  /** Serializes and writes to output_path, atomically.

      An empty output_path means "do not write" (see RecorderConfig), and this
      returns without touching the filesystem -- but only *after* serializing,
      and after handing the serialized trace to the finished-trace sink. That
      order is deliberate and is the non-obvious half: serialize() is what
      calls validate(), so both the early return and the sink sit below the
      one call that can reject the trace, and a --verify-trace run cannot be
      given a trace that could never have been written to disk at all.

      The bytes go to `output_path + ".part"` and are renamed over the target
      only once the stream has closed cleanly, so output_path never holds a
      half-written trace: a disk-full failure mid-write, or a crash before the
      last byte, leaves the target untouched rather than truncated. The partial
      is removed on every *handled* failure path; a SIGKILL or a crash between
      the open and the rename leaves a `<path>.part` behind indefinitely, since
      nothing sweeps them.

      An internally inconsistent trace is rejected by serialize()'s call to
      validate() before any bytes are written; a write failure, a failure
      detected only when the stream is closed (e.g. disk full), and a failed
      rename all throw ParseError.

      Note what atomicity does NOT cover: a *previous* run's trace still sits
      at output_path until this one succeeds. The rename makes "the file at
      this path is complete" true; it cannot make "the file at this path is
      from this run" true, and a caller that cares must remove the target
      first. */
  void write() const;

private:
  /** See set_finished_sink(). Null unless main() installed one. */
  static std::function<void(const Trace&)> s_finished_sink;

  Trace m_trace;
  std::string m_output_path;
  float m_expected_dt;
  /** The dt of the step note_step() last saw, used by record_visual() to turn
      g_game_time into the clock before that step. 0 until the first
      note_step(), so a caller that never calls it records the observed clock
      rather than a wrong one. */
  float m_step_dt;
  ActionMask m_pending_mask;
  std::string m_invalid_reason;
  bool m_awaiting_visual;
  bool m_finished;
  bool m_has_game_time_origin;
  /** Whether this attempt's start state has been settled -- by a successful
      capture or by a refusal. Both count: the recorder asks once per attempt,
      and a refusal is an answer. */
  bool m_has_start_state;
  /** Why the start state could not be captured, empty when it could. Carried
      into the episode note by finish(). */
  std::string m_start_unknown_reason;
  bool m_previous_was_dead;
  bool m_has_previous_sector;
  uint16_t m_previous_sector_id;

private:
  TraceRecorder(const TraceRecorder&) = delete;
  TraceRecorder& operator=(const TraceRecorder&) = delete;
};

} // namespace trace

/* EOF */
