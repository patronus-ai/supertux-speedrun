# Deterministic browser build

The canonical browser build runs in GitHub Actions. Open **Build speedrun WASM**, choose
**Run workflow**, and download the `supertux-wasm-<commit>` artifact when the job completes.
The artifact contains:

- `supertux2.html`
- `supertux2.js`
- `supertux2.wasm`
- `supertux2.data`
- `BUILD_INFO.txt`

The package is intentionally scoped to the `Welcome to Antarctica` challenge. It contains
exactly one playable world level, the image/font/script closure observed while loading that
level, all sound effects, and only the challenge and completion music. CMake also generates its
small internal `levels/misc/menu.stl` scene during configuration. The current staged data is
about 26 MB; CI rejects a `supertux2.data` larger than 40 MiB so a full-game asset copy cannot
slip back in.

Embedders should set `SUPERTUX_START_MUTED=1` before startup and use the exported
`st_set_muted(0|1)` function for an explicit user-controlled mute toggle.

Artifacts are retained for 30 days. The packaged filesystem is mounted at `/data`, so the
fixed challenge level is always `/data/levels/world1/welcome_antarctica.stl` regardless of
which machine produced the build.

`tools/speedrun-data-manifest.txt` records the runtime-opened non-audio file closure and
`tools/stx_stage_speedrun_data.sh` creates the reduced, permanently silent data tree. If this level or its engine
assets change, regenerate the manifest from a full build and smoke-test a complete trace before
shipping the updated artifact.

## Local builds

The build is not Linux-only. It can run on macOS or Linux once emsdk 6.0.8, vcpkg, CMake, and
Ninja are installed. The script no longer contains `/home/ubuntu` paths and accepts its tool
locations through environment variables:

```bash
export EMSDK_ROOT=/absolute/path/to/emsdk
export VCPKG_ROOT=/absolute/path/to/vcpkg
export BUILD_JOBS=4
tools/stx_build_wasm.sh
```

Use the same vcpkg commit recorded in `.github/workflows/wasm-speedrun.yml` and install the
`wasm32-emscripten` dependencies listed in that workflow. Generated files are written to
`build-wasm/` by default. Set `SUPERTUX_BUILD_DIR` to use another output directory.
