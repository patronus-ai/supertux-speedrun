#!/usr/bin/env bash
# Install the wasm32-emscripten dependencies tools/stx_build_wasm.sh links against, into a vcpkg
# checkout pinned to one commit, so the build is reproducible on a machine other than ours.
#
#   EMSDK=/path/to/emsdk VCPKG_ROOT=/path/to/vcpkg tools/stx_setup_deps.sh
#
# EMSDK must already have 6.0.8 activated (DETERMINISM.md): vcpkg's wasm32-emscripten triplet
# compiles every package with the emcc it finds through $EMSDK. VCPKG_ROOT is cloned if absent.
#
# The package list is upstream SuperTux 0.6.3's own (.github/workflows/other.yml, wasm job). The
# libraries the benchmark's build actually linked -- boost atomic/charconv/chrono/container/
# date_time/filesystem/locale/thread, glm, ogg, png16, vorbis(file), z -- all come from it.
set -euo pipefail

EMSDK="${EMSDK:-$HOME/emsdk}"
VCPKG_ROOT="${VCPKG_ROOT:-$HOME/vcpkg}"
VCPKG_COMMIT="${VCPKG_COMMIT:-357b1a6f8343064ca982a20771e420fcecfe7efc}"   # microsoft/vcpkg master, 2026-10-14
PACKAGES=(boost-date-time boost-filesystem boost-format boost-locale boost-optional boost-system
          glbinding libpng libogg libvorbis glm zlib)

[ -f "$EMSDK/emsdk_env.sh" ] || { echo "EMSDK not found at $EMSDK -- install emsdk 6.0.8 first" >&2; exit 2; }
source "$EMSDK/emsdk_env.sh" >/dev/null 2>&1
export EMSDK

if [ ! -d "$VCPKG_ROOT/.git" ]; then
  git clone https://github.com/microsoft/vcpkg.git "$VCPKG_ROOT"
fi
git -C "$VCPKG_ROOT" fetch --quiet origin "$VCPKG_COMMIT" 2>/dev/null || git -C "$VCPKG_ROOT" fetch --quiet origin
git -C "$VCPKG_ROOT" checkout --quiet "$VCPKG_COMMIT"
[ -x "$VCPKG_ROOT/vcpkg" ] || "$VCPKG_ROOT/bootstrap-vcpkg.sh" -disableMetrics

"$VCPKG_ROOT/vcpkg" install --triplet wasm32-emscripten "${PACKAGES[@]}"
echo "STX_DEPS_DONE vcpkg=$(git -C "$VCPKG_ROOT" rev-parse --short=12 HEAD) at $VCPKG_ROOT"
