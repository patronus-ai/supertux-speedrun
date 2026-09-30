#!/usr/bin/env python3
"""Score a SuperTux trajectory: replay a step-indexed tape and report whether the level cleared.

Emits the keys the TAS adapters already parse (reached_goal / goal_frame / died /
max_progress / reject_reasons), so nothing downstream changes.

DETERMINISM IS THE PREMISE. Verified 4/4 bit-identical over 1200 steps including one run under
8-way CPU saturation (docs/supertux-determinism.md). That rests on two things this scorer must
preserve:
  * SUPERTUX_DETERMINISTIC=1 reaches the game via emscripten's ENV, forcing one physics step per
    loop iteration;
  * the harness DRIVES the loop (st_pause_main_loop + st_tick) instead of polling it. Polling
    observed 1180-1199 steps for a fixed 1200-step target, and that jitter alone made runs
    disagree even though the engine was deterministic.
Both are installed via a getter/setter Module hook, because supertux2.html defines its own
Module and a plain assignment gets clobbered.

    python3 stx_score.py tape.json --out result.json [--max-steps 20000]
"""
import argparse, asyncio, os, functools, hashlib, http.server, json, socketserver, tempfile, threading
from pathlib import Path

ap = argparse.ArgumentParser()
ap.add_argument("tape")
ap.add_argument("--out", required=True)
ap.add_argument("--build", default=os.environ.get("STX_BUILD", "build-wasm"),
                help="emscripten output dir (env STX_BUILD; default ./build-wasm)")
ap.add_argument("--level",
                default=None)  # default: <build>/data/levels/world1/welcome_antarctica.stl
ap.add_argument("--max-steps", type=int, default=20000)
ap.add_argument("--seed", type=int, default=12345)
ap.add_argument("--port", type=int, default=0,
                help="0 = let the OS pick a free port (avoids concurrent-run collisions)")
ap.add_argument("--quiet", action="store_true")
a = ap.parse_args()
# The level path is a path INSIDE the game's virtual filesystem, and emscripten preloads the data
# under the ABSOLUTE build directory it was compiled in. So the default must be derived from the
# absolute build dir, and a build is only usable at the path it was built at (re-build, don't copy).
a.build = str(Path(a.build).resolve())
if a.level is None:
    a.level = str(Path(a.build) / "data/levels/world1/welcome_antarctica.stl")

ROOT = Path(a.build).resolve()
res = {
    "scorer": "stx_score.py", "emulator": "supertux-wasm",
    "reached_goal": False, "goal_frame": None, "died": False,
    "max_progress": None, "steps_run": 0, "reject_reasons": [],
}


class H(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def log_message(self, *args):
        pass


socketserver.TCPServer.allow_reuse_address = True
# EPHEMERAL PORT BY DEFAULT (--port 0). The old fixed 8847 collided whenever two scorer runs
# overlapped inside one arm's container, and the loser died with
#     OSError: [Errno 98] Address already in use
# which the harness reports as "scorer produced no result" -- a SILENTLY LOST evaluation, not a
# failed candidate. Six SuperTux arms hit this, worst 18 times in 53 turns. allow_reuse_address
# does not help: it clears TIME_WAIT sockets, not a live listener.
# Binding port 0 lets the OS assign a free one; read it back for the page URL.
_srv = socketserver.TCPServer(("127.0.0.1", a.port),
                              functools.partial(H, directory=str(ROOT)))
a.port = _srv.server_address[1]
threading.Thread(target=_srv.serve_forever, daemon=True).start()

INIT_JS = """
(function () {
  var real = undefined;
  Object.defineProperty(window, 'Module', {
    configurable: true,
    get: function () { return real; },
    set: function (v) { real = v || {}; __stxInit(real); }
  });
  window.__stxInit = function (Module) {
    Module.arguments = ['%LEVEL%'];
    Module.preRun = (Module.preRun || []).concat([function () {
      try { ENV.SUPERTUX_DETERMINISTIC = '1'; } catch (e) {}
      var dir = '/home/web_user/.local/share/supertux2';
      var parts = dir.split('/').filter(Boolean), cur = '';
      for (var i = 0; i < parts.length; i++) { cur += '/' + parts[i]; try { FS.mkdir(cur); } catch (e) {} }
      var cfg = '(supertux-config\\n  (random_seed %SEED%)\\n  (sound_enabled #f)\\n' +
                '  (music_enabled #f)\\n  (fullscreen_enabled #f)\\n)\\n';
      try { FS.writeFile(dir + '/config', cfg); } catch (e) {}
    }]);
  };
})();
window.__stx = {done:false, err:null, finishedAt:null, died:false, deaths:0, wasDead:false, maxX:-1, minX:null, steps:0, hash:0, lvl:false};
"""

DRIVER_JS = r"""
window.__stxRun = (async (maxSteps, tapeJson) => {
  const S = window.__stx, tape = JSON.parse(tapeJson);
  const call = (fn, t) => Module.ccall(fn, t, [], []);
  const NUM = {ArrowRight:39, ArrowLeft:37, ArrowUp:38, ArrowDown:40, Space:32, ShiftLeft:16, Enter:13};
  const key = (name, code, down) => {
    const ev = new KeyboardEvent(down ? 'keydown' : 'keyup',
      {key:name, code:code, keyCode:NUM[code], which:NUM[code], bubbles:true, cancelable:true});
    window.dispatchEvent(ev);
    (document.getElementById('canvas') || document).dispatchEvent(ev);
  };
  const t0 = Date.now();
  while (Date.now() - t0 < 120000) {
    try { if (call('st_in_level','number') === 1) { S.lvl = true; break; } } catch (e) {}
    await new Promise(r => setTimeout(r, 100));
  }
  if (!S.lvl) { S.err = 'never entered a level'; S.done = true; return; }

  call('st_pause_main_loop', null);
  await new Promise(r => setTimeout(r, 250));

  // st_in_level==1 only means a Sector exists -- it can be true while a LevelIntro screen is up
  // and the sector is NOT being simulated. Starting the tape then wastes the whole run: on
  // bonus3/niv-3 a hold-LEFT and a hold-RIGHT tape produced the IDENTICAL state_hash
  // (200779437) over 1500 steps, i.e. inputs did nothing and nothing moved. So require the
  // world to actually change before indexing any input against it.
  {
    // PRE-ROLL: dismiss the LevelIntro screen. Many levels open on it, and until it is popped
    // the sector is NOT simulated while st_in_level() already returns 1 and st_tux_x() reads the
    // spawn -- a level that looks loaded but ignores every arrow key. levelintro.cpp:80-85 pops
    // it only on JUMP / ACTION / MENU_SELECT / START / ESCAPE, so a Space press is what clears
    // it (Enter is the MENU key and PAUSES the game instead -- tapping Enter here froze every
    // level, including ones that run fine untouched).
    //
    // Done here, before the tape, so it is uniform across levels and NOT counted in goal_frame:
    // dismissing an intro is harness setup, not gameplay, and making each agent spend tape steps
    // on it would both pollute the metric and silently zero any tape that forgot.
    // Harmless where no intro exists: Tux simply jumps once and lands within the settle ticks.
    key('Space', 'Space', true);
    try { call('st_tick','number'); } catch (e) {}
    key('Space', 'Space', false);
    for (let w = 0; w < 90; w++) {
      try { call('st_tick','number'); } catch (e) { break; }
      if ((w & 31) === 0) await new Promise(r => setTimeout(r, 0));
    }
  }

  const held = new Set();
  for (let s = 0; s < maxSteps; s++) {
    const acts = tape[String(s)];
    if (acts) for (const [k, c, d] of acts) {
      if (d) { if (!held.has(c)) { held.add(c); key(k, c, true); } }
      else   { if (held.has(c))  { held.delete(c); key(k, c, false); } }
    }
    // A wasm trap inside the tick (seen as "RuntimeError: null function" on cutscene levels,
    // which need context a standalone load does not provide) must NOT take the scorer down --
    // an optimisation loop replays arbitrary agent tapes and has to score a crash as a
    // rejection, not lose the whole turn to a traceback.
    try {
      call('st_tick', 'number');
    } catch (e) {
      S.err = 'st_tick trapped at step ' + s + ': ' + (e && e.message ? e.message : e);
      S.done = true;
      return;
    }
    S.steps = s + 1;
    const x = call('st_tux_x', 'number');
    if (x > S.maxX) S.maxX = x;
    if (S.minX === null || x < S.minX) S.minX = x;
    // Time to st_goal_reached (Tux touches the goal), NOT st_level_finished (which fires only
    // after the victory animation finishes and would inflate every run by its duration).
    if (call('st_goal_reached', 'number') === 1) { S.finishedAt = s + 1; break; }
    // DEATHS ARE NOT TERMINAL -- they are expensive. SuperTux respawns Tux at the level start,
    // so a death already costs ~1000+ steps of re-tread, and goal_frame carries that penalty
    // without needing a separate rule. Treating the first death as the end of the run instead
    // made online runs unscoreable offline: a 10-death clear stopped at step 539 of 9498.
    // st_player_dead() stays true through the whole death ANIMATION, so we must tick through
    // it until Tux is genuinely alive again -- breaking on the first dead=1 and resuming the
    // tape would re-enter the same state and count one death many times over.
    // Do NOT inject respawn ticks here. The tape already CONTAINS the death animation and
    // restart as a no-key gap of the correct length (the converter emits every elapsed step,
    // including <respawn> rows), and the build is deterministic so that gap is exactly right.
    // Ticking extra steps on top double-counts the respawn and shifts the whole tail: doing so
    // turned a 10-death run into 18 deaths and lost the clear. Just record the death -- it is
    // already paid for in goal_frame by ~1000 steps of re-tread from spawn -- and keep going.
    const nowDead = call('st_player_dead', 'number') === 1;
    if (nowDead && !S.wasDead) { S.died = true; S.deaths = (S.deaths || 0) + 1; }
    S.wasDead = nowDead;
    if ((s & 31) === 0) await new Promise(r => setTimeout(r, 0));
  }
  S.hash = call('st_state_hash', 'number');
  S.done = true;
})(%STEPS%, %TAPE%);
"""


async def main():
    from playwright.async_api import async_playwright
    tape = json.loads(Path(a.tape).read_text())
    prof = tempfile.mkdtemp(prefix="stxscore_")
    async with async_playwright() as pw:
        ctx = await pw.chromium.launch_persistent_context(
            prof, args=["--no-sandbox", "--use-gl=swiftshader", "--enable-unsafe-swiftshader"],
            viewport={"width": 800, "height": 600})
        pg = ctx.pages[0] if ctx.pages else await ctx.new_page()
        await pg.add_init_script(INIT_JS.replace("%SEED%", str(a.seed)).replace("%LEVEL%", a.level))
        await pg.goto(f"http://127.0.0.1:{a.port}/supertux2.html", wait_until="load")
        await pg.evaluate(DRIVER_JS.replace("%STEPS%", str(a.max_steps))
                                   .replace("%TAPE%", json.dumps(json.dumps(tape))))
        for _ in range(6000):
            if await pg.evaluate("window.__stx.done"):
                break
            await pg.wait_for_timeout(200)
        st = await pg.evaluate("window.__stx")
        await ctx.close()
    return st


st = asyncio.run(main())
res["steps_run"] = st.get("steps", 0)
res["max_progress"] = st.get("maxX")
res["died"] = bool(st.get("died"))
res["deaths"] = int(st.get("deaths") or 0)
res["state_hash"] = st.get("hash")
if st.get("err"):
    res["reject_reasons"].append(st["err"])
elif st.get("finishedAt"):
    res["reached_goal"] = True
    res["goal_frame"] = st["finishedAt"]           # steps, the game's own clock
else:
    span = None
    if st.get("minX") is not None and st.get("maxX") is not None:
        span = st["maxX"] - st["minX"]
    # A tape that moves Tux nowhere at all means the inputs never reached the game -- e.g. the
    # level sat on an intro/menu screen. bonus3/niv-3 gave the IDENTICAL state_hash for a
    # hold-LEFT and a hold-RIGHT tape; without this the run just looks like a bad attempt.
    if span is not None and span < 1.0:
        res["reject_reasons"].append(
            f"player never moved (x span {span:.2f}px over {res['steps_run']} steps) -- inputs "
            f"are not reaching the game; the level is probably on an intro/menu screen")
    else:
        res["reject_reasons"].append(
            f"level not completed in {res['steps_run']} steps "
            f"(max_x={res['max_progress']}, died={res['died']})")

Path(a.out).write_text(json.dumps(res, indent=1))
if not a.quiet:
    print(json.dumps(res, indent=1))
