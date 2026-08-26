#!/usr/bin/env bash
# Configure + build the PATCHED SuperTux for wasm.
#
# Why we rebuild at all: the stock v0.6.3 WASM release is nondeterministic. Confirmed
# empirically -- a FIXED random_seed reproduces the starting world bit-exactly
# (start hash 51315783d8 twice) but the run still ends differently
# (f1d050894f vs decc60f461). So worldgen is seedable and the *simulation* is not.
#
# Root cause in source (screen_manager.cpp): the physics step SIZE is fixed
# (ms_per_step = 1000/LOGICAL_FPS) but the step COUNT is derived from wall clock
# (steps = elapsed_ticks / ms_per_step), so a slower frame runs more steps. Our patch
# forces elapsed_ticks = ms_per_step under SUPERTUX_DETERMINISTIC=1, making the step
# count a function of frames rather than of how busy the machine was.
#
# -sUSE_PTHREADS=0 is the second half: single-threaded is what lets a virtual clock
# drive the sim without deadlocking against SharedArrayBuffer/pthreads.
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
SRC=${SUPERTUX_SRC_DIR:-$(cd "$SCRIPT_DIR/.." && pwd)}
BUILD_DIR=${SUPERTUX_BUILD_DIR:-$SRC/build-wasm}
STX_EMSDK_ROOT=${EMSDK_ROOT:-${EMSDK:-}}
STX_VCPKG_ROOT=${VCPKG_ROOT:-}
VCPKG_TRIPLET=${VCPKG_TRIPLET:-wasm32-emscripten}
STX_CMAKE_GENERATOR=${SUPERTUX_CMAKE_GENERATOR:-Ninja}

if [[ -z "$STX_EMSDK_ROOT" || ! -f "$STX_EMSDK_ROOT/emsdk_env.sh" ]]; then
  echo "Set EMSDK_ROOT to an activated emsdk checkout." >&2
  exit 2
fi
if [[ -z "$STX_VCPKG_ROOT" || ! -x "$STX_VCPKG_ROOT/vcpkg" ]]; then
  echo "Set VCPKG_ROOT to a bootstrapped vcpkg checkout." >&2
  exit 2
fi
if [[ "$STX_CMAKE_GENERATOR" == "Ninja" ]] && ! command -v ninja >/dev/null 2>&1; then
  echo "Ninja is required (install it with apt, Homebrew, or your package manager)." >&2
  exit 2
fi

if [[ -n "${BUILD_JOBS:-}" ]]; then
  JOBS=$BUILD_JOBS
elif command -v nproc >/dev/null 2>&1; then
  JOBS=$(nproc)
else
  JOBS=$(sysctl -n hw.logicalcpu 2>/dev/null || echo 4)
fi

# SINGLE-INSTANCE LOCK. This script wipes CMakeCache.txt before configuring, so two copies
# racing in one build dir delete each other's state. That produced a bogus
# "Cannot copy output executable ''" at CheckTypeSize TWICE and both times looked like a real
# toolchain fault. Refuse to be the second instance instead.
if command -v flock >/dev/null 2>&1; then
  exec 9>"${TMPDIR:-/tmp}/stx_build.lock"
  if ! flock -n 9; then
    echo "Another SuperTux WASM build is already running." >&2
    exit 3
  fi
fi

# shellcheck source=/dev/null
source "$STX_EMSDK_ROOT/emsdk_env.sh" >/dev/null 2>&1
cd "$SRC"

# Upstream applies this to the SDL_ttf submodule; a re-apply is a harmless no-op.
if [ -d external/SDL_ttf ]; then
  ( cd external/SDL_ttf && git apply ../../mk/emscripten/SDL_ttf.patch 2>/dev/null \
      && echo "SDL_ttf patch applied" || echo "SDL_ttf patch already applied / not needed" )
fi

mkdir -p "$BUILD_DIR"

# Stage the game DATA into the build dir before configuring. CMake only generates one file
# there (CMakeLists.txt:1205 configure_file for levels/misc/menu.stl); the 245 MB data/ tree is
# rsynced in by upstream's CI as a separate step (.github/workflows/other.yml:183), outside
# CMake. Skip it and --preload-file happily packages a single file: the first build produced a
# 555 KB supertux2.data with no levels at all, and nothing errored -- an engine with no content,
# which would have made any determinism check a measurement of the menu screen.
#
# Keep music and sound in the browser package. The embedding site starts the engine muted via
# SUPERTUX_START_MUTED and exposes a user-controlled mute button; packaging the assets is what
# lets audio begin immediately when the user opts in.
echo "=== staging data/ into build-wasm/data ==="
rsync -a --delete-after data/ "$BUILD_DIR/data/"
du -sh "$BUILD_DIR/data" | sed 's/^/  staged: /'
find "$BUILD_DIR/data" -name '*.stl' | wc -l | sed 's/^/  level files staged: /'

cd "$BUILD_DIR"

# CMake will NOT swap compilers on a re-configure -- the failed host-compiler run is baked
# into the cache, so it must go or we re-detect /usr/bin/c++ and fail identically.
rm -rf CMakeCache.txt CMakeFiles Makefile build.ninja cmake_install.cmake

echo "=== CMAKE CONFIGURE ==="
# vcpkg and emscripten BOTH want to be CMAKE_TOOLCHAIN_FILE, and passing vcpkg's explicitly
# beats the one `emcmake` exports -- so the first attempt configured against the host
# /usr/bin/c++ and then rejected vcpkg's Boost as "(32bit)". The version was never the
# problem. VCPKG_CHAINLOAD_TOOLCHAIN_FILE is how the two compose: vcpkg stays the entry
# point and loads the emscripten toolchain underneath, so emcc is the actual compiler.
# (The wasm32-emscripten triplet sets CHAINLOAD for vcpkg's own package builds only, which
# is why the dependencies built fine while the consuming project did not.)
EM_TC="$STX_EMSDK_ROOT/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"
VLIB="$STX_VCPKG_ROOT/installed/$VCPKG_TRIPLET/lib"

# FindOggVorbis.cmake:37 does check_library_exists(vorbis ...), emitting a bare -lvorbis that
# wasm-ld cannot resolve without vcpkg's lib dir on the search path. Passing
# -DCMAKE_EXE_LINKER_FLAGS does NOT work: CMakeLists.txt:93 OVERWRITES that variable while
# interpolating ${CMAKE_LINKER_FLAGS} -- an upstream typo (the intended name has _EXE_). So we
# feed the -L through CMAKE_LINKER_FLAGS (the name it actually reads) and through
# C/CXX_FLAGS, which lines 86-87 append to rather than clobber. Belt and braces, because
# check_library_exists links via the C flags while the real link uses the linker flags.
#
# -DEMSCRIPTEN is required because SuperTux's sources guard on the bare macro, e.g.
# src/addon/downloader.hpp:21 `#ifndef EMSCRIPTEN` around <curl/curl.h> (CMakeLists.txt:375
# deliberately skips find_package(CURL) for emscripten, so no curl exists to include).
# emsdk 1.40.1 -- which upstream pins -- injected -DEMSCRIPTEN into the C flags from its
# toolchain; modern emsdk defines only __EMSCRIPTEN__, so every such guard silently flips and
# the build tries to include headers that were never provided.
#
# SIZEOF_VOID_P is pre-seeded because ConfigureChecks.cmake:14 CHECK_TYPE_SIZE("void*") cannot
# work here: it try_compiles then COPY_FILEs the executable to read an embedded size string,
# but CMakeLists.txt:79 sets CMAKE_EXECUTABLE_SUFFIX=.html for emscripten, so CMake looks for
# cmTC_*.html while the size string lives in the .wasm -- it fails as "Cannot copy output
# executable ''". wasm32 pointers are 4 bytes, so seeding the cache skips a probe whose answer
# we already know. (The neighbouring "functions that differ only in their return type" error in
# CMakeError.log is NOT a fault: ConfigureChecks.cmake:4-12 deliberately compiles a clashing
# iconv declaration to detect the const-ness of the platform's iconv.)
# CMAKE_HAVE_LIBC_PTHREAD=1: boost_atomic's config does an unconditional
# find_dependency(Threads), and FindThreads probes `-pthread` -- which fails because we
# deliberately build with -sUSE_PTHREADS=0 (CMakeLists.txt:82). Asserting that libc
# provides threads satisfies FindThreads with EMPTY link flags, which is exactly right for
# wasm and preserves the single-threaded build. Enabling real pthreads would "fix" the
# configure and defeat the point: pthreads/SharedArrayBuffer is what deadlocks the virtual
# clock we need to drive the sim deterministically.
emcmake cmake .. -G "$STX_CMAKE_GENERATOR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_OPENGLES2=ON \
  -DCMAKE_TOOLCHAIN_FILE="$STX_VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
  -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$EM_TC" \
  -DVCPKG_TARGET_TRIPLET="$VCPKG_TRIPLET" \
  -DCMAKE_HAVE_LIBC_PTHREAD=1 \
  -DTHREADS_PREFER_PTHREAD_FLAG=OFF \
  -DIconv_IS_BUILT_IN=TRUE \
  -DCMAKE_LIBRARY_PATH="$VLIB" \
  -DCMAKE_LINKER_FLAGS="-L$VLIB" \
  -DCMAKE_C_FLAGS="-L$VLIB -DEMSCRIPTEN" \
  -DCMAKE_CXX_FLAGS="-L$VLIB -DEMSCRIPTEN" \
  -DHAVE_SIZEOF_VOID_P=TRUE \
  -DSIZEOF_VOID_P=4
echo "cmake_exit=0"

echo "=== BUILD ==="
# CMake's nested ExternalProject configure steps repeatedly lost GNU Make's jobserver children
# on GitHub-hosted runners ("wait: No child processes" followed by SIGTERM). Ninja avoids that
# process-accounting path and works for both the top-level project and its nested dependencies.
#
# Emscripten configure probes can then run silently for several minutes. Keep producing output
# while the real build is alive so hosted runners/proxies do not treat that silence as a stalled
# command and send SIGTERM (the observed failure was Ninja exit 143 after 156 quiet seconds).
set +e
emmake cmake --build . --parallel "$JOBS" &
STX_BUILD_PID=$!
while kill -0 "$STX_BUILD_PID" 2>/dev/null; do
  sleep 20
  if kill -0 "$STX_BUILD_PID" 2>/dev/null; then
    echo "build heartbeat: Ninja is still running"
  fi
done
wait "$STX_BUILD_PID"
STX_BUILD_STATUS=$?
set -e
if [[ "$STX_BUILD_STATUS" -ne 0 ]]; then
  echo "Browser build failed with exit code $STX_BUILD_STATUS" >&2
  exit "$STX_BUILD_STATUS"
fi
echo "build_exit=0"

# Install SuperTux's own HTML shell over emscripten's default one. CMakeLists.txt:1157
# configure_file()s template.html.in into the build dir but never installs it; upstream's CI
# does `rm supertux2.html && cp template.html supertux2.html` as a separate manual step
# (.github/workflows/other.yml:185). Skipping it ships emscripten's stock shell, which lacks
# window.supertux_loadFiles -- gameconfig.cpp:93 calls that unconditionally, so main() aborted
# with "supertux_loadFiles is not defined", /data never mounted and calledRun stayed false.
# The game looked built and launched to a blank 300x150 canvas.
if [ -f template.html ]; then
  cp -f template.html supertux2.html || exit 1
  if grep -q 'supertux_loadFiles' supertux2.html; then
    echo "  installed SuperTux html shell (supertux_loadFiles present)"
  else
    echo "  ERROR: shell installed but supertux_loadFiles missing"
    exit 1
  fi
else
  echo "  ERROR: template.html not generated -- expected from CMakeLists.txt:1157"; exit 1
fi

for artifact in supertux2.html supertux2.js supertux2.wasm supertux2.data; do
  if [[ ! -s "$artifact" ]]; then
    echo "Missing or empty build artifact: $BUILD_DIR/$artifact" >&2
    exit 1
  fi
done
ls -lah supertux2.{html,js,wasm,data}
echo STX_BUILD_DONE
echo "Artifacts: $BUILD_DIR"
