# Ghosts and replay capture — integration guide

Wiring guide for the ghost and replay-capture code ported from
`patronus-ai/supertux-ghosts`.

**The code is in `src/` and compiles** — a clean build from scratch is 525/525 with zero
errors. What it does not yet do is anything: nothing calls into it, so the linker drops it
from the final binary and the game behaves exactly as before. This document is the wiring.

Every call site cited as `game_session.cpp:NNNN` is a line in the **source fork**, given so
the shape is visible and your equivalent findable. The 0.6.3 API differences that the port
already handles are listed in the PR description; what follows is the integration those
files still need.

---

## What is already done

`src/trace/` (the format, reader/writer, comparator, ghost playback and the store rule),
`src/object/ghost*` and `src/supertux/trace_{recorder,sampler}` are ported, adapted to
0.6.3's APIs, and compiling. The Python tools in `tools/trace/` run as-is.

Skip to step 2 — step 1 below is kept because the verification in it is still worth doing
once, before you trust anything downstream.

## Step 1 — verify the format layer agrees across languages

`supertux_trace.py` is an independent implementation of the same format. Record or
hand-author a trace, read it with both, and confirm they agree. If they do not, stop:
everything downstream assumes they do.

## Step 1 — take `src/trace/` first, on its own

Copy `src/trace/` to `src/trace/`. It should compile with one
adaptation: `trace_control_map.hpp` maps our `Control` enum to the format's
stable action bit order. Point it at your `Control`, keeping the *numeric bit
order* in `trace_action.hpp` unchanged — those integers are on disk.

Verify before going further: build `supertux_trace.py`'s golden round trip
against the C++ writer. If the two disagree, stop — everything downstream
assumes they agree.

## Step 2 — sampling, and the one ordering contract that matters

Recording is three calls per logical step, and **the order is a contract**, not
a style choice:

```
  note_step(dt, speed_multiplier)      // opens the step
  record_inputs(sample_actions(...))   // BEFORE the world updates
  … sector->update(dt) …               // the world moves
  record_visual(snapshot, sector_name) // AFTER, from the settled state
```

In our tree: `game_session.cpp:1219`, `:1259`, then the sector update, then the
snapshot at `:1300`. Inputs are ground truth; the visual track is a derived
cache. For every step `n`, `visual[n]` is the result of applying `inputs[n]`.

Sample the visual from `Player`: position, velocity, sprite action name,
direction, visible, active.

**A recording fault must never take down a live game.** Our hook wraps the whole
thing and, on any exception, logs and drops the recorder — the partial trace is
lost, the player's session is not (`game_session.cpp:1263-1268`).

## Step 3 — the trap that will cost you a day if you skip it

**Tux's sprite action must be chosen after the collision phase, not inside
`draw()`.**

In stock SuperTux the sprite action is selected during `Player::draw()`. Any
loop that skips the draw pass therefore records `action`, `frame_idx` and
`frame_progress` **frozen at their initial values** for the entire run. A ghost
fed such a trace renders one pose forever and looks like a rendering bug. It is
not — the defect is in the input.

We moved selection into `Player::update_sprite_action()`, called from
`Sector::update()` *post-collision*. Two ways not to undo it: putting it in
`Player::update()`'s tail collapses the run to one airborne action, because
`update()` ends by clearing the on-ground flag; and leaving the `draw()` call in
place double-consumes the idle timer.

**Does this affect you?** Your harness draws once per `st_tick()`, so your
sprite fields are probably live. Check before assuming: an actions table of
length 1 across a multi-hundred-step trace is the signature. `GhostPlayback`
refuses such a trace at load with a message naming the cause.

## Step 4 — the ghosts

`GhostObject` is a `GameObject` and deliberately **not** a `MovingObject`. Four
properties are structural, not review notes:

1. `is_saveable()` returns `false`, so the editor can never write a ghost into a
   level file.
2. No collision registration — the collision system never sees it.
3. It draws only. It names neither RNG stream and writes to nothing outside
   itself.
4. Its drawing is gated on a switch that leaves its simulation running, so a
   ghost re-enabled later is still in the right place.

**Its step clock is exactly one step per `update()` call and deliberately
ignores `dt_sec`.** A trace is step-indexed, never wall-clock-indexed.
Accumulating `dt` here would reintroduce the wall clock the whole exercise
removes — which matters more in your build than ours.

Hooks, in our tree:

- construct the manager and `load(paths)` — `game_session.cpp:235`
- `attach_to_sector(sector, name, restart)` after the sector is activated, on
  every restart **and** every sector change — `:876` and `:1600`. A `Sector`
  owns its objects, so a transition leaves the old ghosts behind in a sector
  nothing updates any more.
- `draw_markers(context, sector)` in the draw path — `:1146`. Deliberately
  *outside* the sector's own draw, so the render switch cannot disable anything
  else.

`GhostManager::load()` appends but **rethrows on the first bad file**, naming
it. That is right for paths a person typed and wrong for a directory you
discovered — load one path per call inside its own `try` if you scan a folder.

**`ghost_manager.cpp` includes `supertux/trace_executor.hpp`** purely for
`read_file_bytes()` / `deserialize()`. We reused the executor's reading path
rather than write a second one. You have no executor: either lift those two
helpers or point them at your own loader. This is the one include in the drop
that will not resolve for you.

## Step 5 — the restart semantics have a known defect

`attach_to_sector(..., restart=true)` rewinds every ghost to step 0. Correct for
an ordinary death, where the player also returns to the level start.

**Wrong after a checkpoint.** The player resumes at the bell while the ghosts
restart thousands of pixels behind, and the race silently stops existing. We
found this by playing, not by testing — every one of our probes starts at level
start and none uses a checkpoint. Measured on `welcome_antarctica`: the player
respawning at x=5360 with the ghosts stranded at x=96.

Unfixed in our tree as of this drop. If you wire ghosts up, decide what a
checkpoint should do — most likely rewind the ghost to the step nearest the
checkpoint rather than to zero.

## Step 6 — a trace is only a ghost if it says `AGENT`

The selection rule we ship is *agent traces in, human runs out*: a recorded
player run lands in the same directory and is skipped, which is the only thing
stopping it replaying itself as a ghost forever.

**Our recorder hardcodes `producer_kind = HUMAN`** for everything it writes.
Nothing in either repository produces an `AGENT`-tagged trace on its own, so:

```
uv run python retag_producer.py IN.stgt OUT.stgt agent
```

Three positional arguments on purpose — an in-place default would let one bad
run destroy the only copy of an agent's run, and there is no flag to re-record
one.

## Step 7 — what your tapes become

`tape_convert.py` turns `{"125": [["ArrowRight","ArrowRight",0], …]}` into
action masks. **A transition applies AT the step it is stamped on, not after
it** — your own `decoded/*.txt` settled that for us, across all eleven runs,
step for step. An unknown key is refused rather than dropped, because a fourth
key means the corpus was misread.

We executed all eleven on our engine. Eight reach the level's end sequence,
`dspro` with zero deaths — despite our `LOGICAL_FPS` being `1000/15` against
your `64`. Your tapes transfer far better than that gap predicts.

---

## Suggested order

1. `src/trace/` alone, with the golden round trip green (step 1).
2. Recording, with the ordering contract (step 2) — check step 3's trap first.
3. Record one run, load it in `supertux_trace.py`, confirm the visual track
   moves and the actions table has more than one entry.
4. Ghosts (step 4), fed by that trace retagged to `AGENT` (step 6).
5. Only then wire your tapes through (step 7).

Each stage is verifiable on its own. Do not do them at once — the failure modes
look identical from outside, and that is how a day disappears.

## Two questions we still owe you

1. **What does the number in a tape's filename mean?** `dspro` has zero deaths,
   so our recorded count is its whole run — and it is 2055, exactly the number
   in its filename. The 4% timestep difference predicts ~85 steps of
   disagreement, so an exact match is *more* surprising, not less.
2. **Were the models queried per step, or did they author a tape and iterate on
   its time?** The `offline/` directory name suggests the latter.
