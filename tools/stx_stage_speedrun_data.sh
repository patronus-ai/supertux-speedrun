#!/usr/bin/env bash
# Stage the fixed SpeedrunBench challenge instead of packaging every SuperTux level.
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 SOURCE_DATA_DIR DESTINATION_DATA_DIR" >&2
  exit 2
fi

SOURCE_DATA_DIR=$1
DESTINATION_DATA_DIR=$2
SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
MANIFEST="$SCRIPT_DIR/speedrun-data-manifest.txt"

if [[ ! -d "$SOURCE_DATA_DIR" || ! -f "$MANIFEST" ]]; then
  echo "Missing SuperTux data directory or SpeedrunBench manifest." >&2
  exit 2
fi

mkdir -p "$DESTINATION_DATA_DIR"

# The manifest is the observed startup image/font/script closure for Welcome to Antarctica.
# Sounds, image particles, power-ups, projectiles, explosions, and their light sprites are
# opened only when their actions occur, so retain those small trees. Keep the challenge music,
# completion music, shaders, particle definitions, and speech as conservative runtime
# dependencies. rsync's delete flags ensure an older full-game staging tree is actually pruned.
rsync -a --delete --delete-excluded \
  --include='*/' \
  --include-from="$MANIFEST" \
  --include='/sounds/***' \
  --include='/images/particles/***' \
  --include='/images/powerups/***' \
  --include='/images/objects/bullets/***' \
  --include='/images/objects/coin/***' \
  --include='/images/objects/explosion/***' \
  --include='/images/objects/lightmap_light/***' \
  --include='/particles/***' \
  --include='/shader/***' \
  --include='/speech/***' \
  --include='/music/' \
  --include='/music/antarctic/' \
  --include='/music/antarctic/chipdisko.music' \
  --include='/music/antarctic/chipdisko.ogg' \
  --include='/music/misc/' \
  --include='/music/misc/leveldone.ogg' \
  --include='/ACKNOWLEDGEMENTS.txt' \
  --include='/AUTHORS' \
  --include='/credits.stxt' \
  --exclude='*' \
  "$SOURCE_DATA_DIR/" "$DESTINATION_DATA_DIR/"

required=(
  "levels/world1/welcome_antarctica.stl"
  "images/tiles.strf"
  "fonts/SuperTux-Medium.ttf"
  "fonts/Roboto-Regular.ttf"
  "music/antarctic/chipdisko.ogg"
  "music/misc/leveldone.ogg"
  "sounds/jump.wav"
  "images/particles/smoke.sprite"
  "images/particles/smoke-1.png"
  "images/objects/lightmap_light/lightmap_light-tiny.sprite"
  "images/objects/explosion/explosion.sprite"
  "images/objects/bullets/firebullet.sprite"
  "images/powerups/fireflower/fireflower.sprite"
)
for asset in "${required[@]}"; do
  if [[ ! -s "$DESTINATION_DATA_DIR/$asset" ]]; then
    echo "Required SpeedrunBench asset was not staged: $asset" >&2
    exit 1
  fi
done

level_count=$(find "$DESTINATION_DATA_DIR/levels" -type f -name '*.stl' | wc -l | tr -d ' ')
if [[ "$level_count" != "1" ]]; then
  echo "Expected exactly one staged level, found $level_count" >&2
  exit 1
fi

echo "Staged the Welcome to Antarctica challenge data ($level_count level)."
