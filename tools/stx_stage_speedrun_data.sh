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
# Image particles, power-ups, projectiles, explosions, and their light sprites are opened only
# when their actions occur, so retain those small trees. This browser benchmark is permanently
# silent, so music, effects, and speech are intentionally absent. rsync's delete flags ensure
# an older audio-bearing staging tree is actually pruned.
rsync -a --delete --delete-excluded \
  --include='*/' \
  --include-from="$MANIFEST" \
  --include='/images/particles/***' \
  --include='/images/powerups/***' \
  --include='/images/objects/bullets/***' \
  --include='/images/objects/coin/***' \
  --include='/images/objects/explosion/***' \
  --include='/images/objects/lightmap_light/***' \
  --include='/particles/***' \
  --include='/shader/***' \
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

for audio_dir in music sounds speech; do
  if find "$DESTINATION_DATA_DIR/$audio_dir" -type f -print -quit 2>/dev/null | grep -q .; then
    echo "Audio file was unexpectedly staged under: $audio_dir" >&2
    exit 1
  fi
done

level_count=$(find "$DESTINATION_DATA_DIR/levels" -type f -name '*.stl' | wc -l | tr -d ' ')
if [[ "$level_count" != "1" ]]; then
  echo "Expected exactly one staged level, found $level_count" >&2
  exit 1
fi

echo "Staged the Welcome to Antarctica challenge data ($level_count level)."
