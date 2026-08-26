# Deterministic browser build

The canonical browser build runs in GitHub Actions. Open **Build speedrun WASM**, choose
**Run workflow**, and download the `supertux-wasm-<commit>` artifact when the job completes.
The artifact contains:

- `supertux2.html`
- `supertux2.js`
- `supertux2.wasm`
- `supertux2.data`
- `BUILD_INFO.txt`

The package includes the complete music and sound trees. Embedders should set
`SUPERTUX_START_MUTED=1` before startup and use the exported `st_set_muted(0|1)` function for
an explicit user-controlled mute toggle.

Artifacts are retained for 30 days. The packaged filesystem is mounted at `/data`, so the
fixed challenge level is always `/data/levels/world1/welcome_antarctica.stl` regardless of
which machine produced the build.

## Local builds

The build is not Linux-only. It can run on macOS or Linux once emsdk 6.0.8 and vcpkg are
installed. The script no longer contains `/home/ubuntu` paths and accepts its tool locations
through environment variables:

```bash
export EMSDK_ROOT=/absolute/path/to/emsdk
export VCPKG_ROOT=/absolute/path/to/vcpkg
export BUILD_JOBS=4
tools/stx_build_wasm.sh
```

Use the same vcpkg commit recorded in `.github/workflows/wasm-speedrun.yml` and install the
`wasm32-emscripten` dependencies listed in that workflow. Generated files are written to
`build-wasm/` by default. Set `SUPERTUX_BUILD_DIR` to use another output directory.
