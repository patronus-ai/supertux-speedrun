# Making SuperTux deterministic (WASM, headless)

**Result:** 4/4 bit-identical runs of a 1,200-step tape, *including one under 8-way CPU
saturation* — digest `b62258cb315810bf`, `steps_sampled=1200` and `distinct_pos=994` in every
run. Determinism is input-keyed, not idle-machine luck.

**Upstream base:** SuperTux `c1ddb4f`, emsdk 6.0.8, vcpkg `wasm32-emscripten`.
**Build script:** `/home/ubuntu/stx_build_wasm.sh` (encodes every step below).
**Verifier:** `/home/ubuntu/stx_det_check.py`.

---

## The two-part problem

Stock SuperTux is **not** deterministic, and seeding does not fix it. A fixed `random_seed`
reproduces the *starting world* bit-exactly (start hash `51315783d8` twice) while the *outcome*
still diverges (`f1d050894f` vs `decc60f461`). So worldgen is seedable and the simulation is not.

The cause is in `src/supertux/screen_manager.cpp`. The physics step **size** is fixed
(`ms_per_step = 1000 / LOGICAL_FPS`) but the step **count** is derived from wall clock:

```cpp
Uint32 ticks = SDL_GetTicks();
elapsed_ticks += ticks - last_ticks;      // wall clock
...
if (elapsed_ticks > ms_per_step * 8) elapsed_ticks = 0;   // large-jump reset
```

Identical inputs therefore run a *different number of steps* depending on machine load, and the
run diverges. This is the same defect Tuxemon has (`client.py` accumulator loop).

## Fix 1 — force one step per iteration (necessary, not sufficient)

`screen_manager.cpp`, gated on an env var so native builds are untouched:

```cpp
static const bool s_deterministic = (getenv("SUPERTUX_DETERMINISTIC") != nullptr);
if (s_deterministic) {
  elapsed_ticks = ms_per_step;
  ticks = last_ticks = g_deterministic_ticks;
  g_deterministic_ticks += ms_per_step;
  ++g_deterministic_steps;      // exported as st_step_count()
}
```

This removes wall clock from the *loop body*. It is **not enough on its own**: under emscripten
the loop is scheduled by the browser (`emscripten_set_main_loop(g_loop_iter, -1, 1)` → rAF), so
how many iterations happen in a given span is still the browser's decision.

## Fix 2 — drive the loop, don't watch it (the decisive change)

Added to `src/port/emscripten.hpp`:

| export | purpose |
|---|---|
| `st_pause_main_loop()` | `emscripten_cancel_main_loop()` — take scheduling from the browser |
| `st_tick()` | run exactly one `ScreenManager::loop_iter()` synchronously (it is public) |
| `st_state_hash()` | FNV-1a over every `MovingObject` position, quantised to 1/256 px |
| `st_step_count()` / `st_tux_x()` / `st_tux_y()` / `st_in_level()` | observability |

The harness then does: `st_pause_main_loop()`, and per step — apply the tape's inputs, call
`st_tick()`, sample state. Inputs land on exact steps **by construction**, and the step count is
identical every run because the harness decides it.

## Why this mattered more than the C++ patch

Before driving the loop, the harness polled `st_step_count` from a `setTimeout(0)` loop. That
produced a **false negative**: 4 distinct digests, reported as "SuperTux is non-deterministic."
The engine was fine; the *observer* was jittery — `steps_sampled` wandered 1180–1199 for a fixed
1,200-step target, and an input meant for step 200 landed wherever a poll happened to fall.

Two lessons that generalise:

1. **Compare state at matching step numbers, not sampled sequences.** A digest over the sampled
   sequence differs whenever the sample *count* differs, even from a perfectly deterministic
   engine. The step-anchored check (intersect step numbers across runs, compare state at each)
   separates engine divergence from observer jitter.
2. **A hash over a constant proves nothing.** The empty-tape control had `distinct_pos=1` (Tux
   stands still with no input), so "0 steps differ" was trivially true and carried no
   information. `st_state_hash()` exists because the level's other actors keep moving with no
   input, giving a control run something real to disagree about.

Guards now in the verifier, each earned by a false verdict: require a live level
(`st_in_level==1`), require >50 samples, require the observable to actually change, and print
`steps_sampled` so a broken driver is visible rather than silently producing matching digests.

## Build steps that are NOT in CMake

Upstream's wasm build has manual steps in CI (`.github/workflows/other.yml`), and skipping them
fails silently:

| step | line | symptom if skipped |
|---|---|---|
| `rsync -aP ../data/ data/` | 183 | 555 KB `supertux2.data`, **no levels** — an engine with no content |
| `rm supertux2.html && cp template.html supertux2.html` | 185 | `supertux_loadFiles is not defined`, `main()` aborts, `/data` never mounts |

Plus one of ours: `--preload-file` receives an **absolute** host path, so data lands at
`/home/ubuntu/supertux-src/build-wasm/data/...` inside MEMFS. `main.cpp` splits a level argv into
dirname/basename and mounts the dirname, so a *relative* level path resolves against MEMFS root
and fails. **Pass the level as an absolute MEMFS path.**

`music/` (87 MB of 245 MB) is excluded from the preload — audio is disabled for determinism and
preloaded files live in in-memory MEMFS. SuperTux logs "using dummy sound file" and continues.

## emsdk 6.0.8 vs upstream's pinned 1.40.1

Four of ten build blockers were this mismatch. Worth pinning 1.40.1 instead if starting over:

- `-sUSE_FREETYPE=2` → `=1` (became a bool)
- `-sEXTRA_EXPORTED_RUNTIME_METHODS` removed → use `EXPORTED_RUNTIME_METHODS`
- **`-DEMSCRIPTEN` no longer auto-defined** — sources guard on the bare macro
  (`downloader.hpp:21 #ifndef EMSCRIPTEN` around `<curl/curl.h>`), so guards silently flip
- `CHECK_TYPE_SIZE("void*")` cannot work with `CMAKE_EXECUTABLE_SUFFIX=.html`; pre-seed
  `-DHAVE_SIZEOF_VOID_P=TRUE -DSIZEOF_VOID_P=4`

Non-emsdk blockers: `CMakeLists.txt:93` overwrites `CMAKE_EXE_LINKER_FLAGS` while interpolating
`${CMAKE_LINKER_FLAGS}` (an upstream typo — pass `-L` via `CMAKE_LINKER_FLAGS`/`C_FLAGS`
instead); squirrel builds shared libs unless `-DDISABLE_DYNAMIC=ON` (wasm has no `.so`);
`boost_atomic` needs `Threads` (`-DCMAKE_HAVE_LIBC_PTHREAD=1`); iconv is built into emscripten
libc (`-DIconv_IS_BUILT_IN=TRUE`); and vcpkg must be the entry point with
`VCPKG_CHAINLOAD_TOOLCHAIN_FILE` pointing at emscripten's toolchain, or the host `c++` gets
selected and vcpkg's Boost is rejected as "(32bit)".

## Still open

- Verified on **one level, one 1,200-step tape**. Confirm on a longer run and a second level.
- **No terminal condition exported.** We can read position but not "level complete", which a
  speedrun objective needs.
- Build carries local patches + two manual steps; not reproducible from a clean checkout without
  `stx_build_wasm.sh`.
