#!/usr/bin/env bash
# Full host verification without the physical panel.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SIM="$ROOT/simulator"
OUT="${SIM_OUT:-/tmp/tessara-sim-out}"
mkdir -p "$OUT"

echo "==> native suite"
cd "$ROOT"
.venv/bin/pio test -e native

echo "==> configure & build simulator"
cmake -S "$SIM" -B "$SIM/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "$SIM/build" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

PAINEL="$SIM/build/painel"
test -x "$PAINEL"

echo "==> scenario screenshots + compose asserts"
SCENARIOS=(
  normal offline sem_local sem_bateria widgets_no_weather widgets_with_clock widgets_with_hero weather_loading
  cold_offline weather_timeout invalid_payload stale_cache
)
for name in "${SCENARIOS[@]}"; do
  echo "--- home/$name"
  SDL_VIDEODRIVER=dummy "$PAINEL" \
    --fixtures "$SIM/fixtures" \
    --scenario "$name" \
    --screen home \
    --viewport 320x480 \
    --screenshot "$OUT/$name.bmp" \
    --headless | tee "$OUT/$name.log"
  test -s "$OUT/$name.bmp"
done

echo "==> rotation (landscape) smoke"
SDL_VIDEODRIVER=dummy "$PAINEL" \
  --fixtures "$SIM/fixtures" \
  --scenario normal \
  --screen home \
  --viewport 480x320 \
  --screenshot "$OUT/rotation-landscape.bmp" \
  --headless | tee "$OUT/rotation-landscape.log"
test -s "$OUT/rotation-landscape.bmp"

echo "==> detail screens (normal)"
for screen in todoist weather aitokens calendar clock settings; do
  echo "--- detail/$screen"
  SDL_VIDEODRIVER=dummy "$PAINEL" \
    --fixtures "$SIM/fixtures" \
    --scenario normal \
    --screen "$screen" \
    --viewport 320x480 \
    --screenshot "$OUT/detail-$screen.bmp" \
    --headless | tee "$OUT/detail-$screen.log"
  test -s "$OUT/detail-$screen.bmp"
  grep -q "screen=$screen" "$OUT/detail-$screen.log"
done

echo "==> clock unsynced detail (cold_offline)"
SDL_VIDEODRIVER=dummy "$PAINEL" \
  --fixtures "$SIM/fixtures" \
  --scenario cold_offline \
  --screen clock \
  --viewport 320x480 \
  --screenshot "$OUT/detail-clock-unsynced.bmp" \
  --headless | tee "$OUT/detail-clock-unsynced.log"
test -s "$OUT/detail-clock-unsynced.bmp"
grep -q "screen=clock" "$OUT/detail-clock-unsynced.log"

# Spot-check expected compose lines from logs
grep -q 'tiles=3' "$OUT/normal.log"
grep -q 'screen=home' "$OUT/normal.log"
grep -q 'weather=0' "$OUT/widgets_no_weather.log"
grep -q 'todoist=1' "$OUT/widgets_no_weather.log"
grep -q 'clock_tile=1' "$OUT/widgets_with_clock.log"
grep -q 'clock_tile=1' "$OUT/widgets_with_hero.log"
grep -q 'tiles=6' "$OUT/widgets_with_hero.log"
grep -q 'wifi=0' "$OUT/offline.log"
grep -q 'wifi=0' "$OUT/cold_offline.log"

echo "==> OK — artifacts in $OUT"
ls -la "$OUT"
