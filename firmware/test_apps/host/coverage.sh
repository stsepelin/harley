#!/usr/bin/env bash
# Build with gcov instrumentation, run the tests, extract the policy-scoped
# subset, produce an HTML report, and open it. Idempotent — safe to re-run.
#
# Usage: ./coverage.sh [--no-open]
set -euo pipefail

cd "$(dirname "$0")"

# Apple's clang produces gcov data, but lcov defaults to `gcov` from PATH.
# On macOS that may be missing or be the wrong version. Pick whichever
# gcov-compatible binary actually exists.
if command -v gcov >/dev/null 2>&1; then
    GCOV_TOOL="gcov"
elif command -v llvm-cov >/dev/null 2>&1; then
    # Shim wrapper so `gcov ...` works even though we want `llvm-cov gcov ...`.
    SHIM="$(mktemp -d)/gcov"
    cat > "$SHIM" <<'EOF'
#!/bin/sh
exec llvm-cov gcov "$@"
EOF
    chmod +x "$SHIM"
    GCOV_TOOL="$SHIM"
else
    echo "Need gcov or llvm-cov on PATH (try: brew install lcov)." >&2
    exit 1
fi

if ! command -v lcov >/dev/null 2>&1; then
    echo "Need lcov on PATH (try: brew install lcov)." >&2
    exit 1
fi

# Files under the coverage policy. Must match CI workflow's `lcov --extract`
# list and the table in README.md.
SCOPED=(
    '*/main/simulator/gear_table.c'
    '*/main/simulator/sim_math.c'
    '*/main/display/format.c'
    '*/main/display/gesture.c'
    '*/main/display/units.c'
    '*/main/display/widgets/smooth.c'
    '*/main/display/widgets/fuel_scale.c'
    '*/main/phone/phone_data.c'
    '*/main/phone/phone_protocol.c'
    '*/main/settings/settings.c'
    '*/main/vehicle/vehicle_data.c'
    '*/main/vehicle/gear_calc.c'
    '*/main/ble/ble_visibility.c'
    '*/main/j1850/j1850_vpw.c'
    '*/main/j1850/j1850_tx_logic.c'
    '*/main/j1850/j1850_parse.c'
    '*/main/j1850/j1850_driver.c'
    '*/main/j1850/j1850_edge.c'
    '*/main/j1850/ride_log_format.c'
    '*/main/display/widgets/sprite_raster.h'
    '*/main/display/widgets/fuel_arc.c'
    '*/main/display/widgets/gear_indicator.c'
    '*/main/display/widgets/speed_display.c'
    '*/main/display/widgets/notification_banner.c'
    '*/main/display/widgets/media_banner.c'
    '*/main/display/widgets/now_playing_display.c'
    '*/main/display/widget_util.c'
    '*/main/display/widgets/temp_display.c'
    '*/main/display/widgets/turn_signals.c'
    '*/main/display/widgets/clock_display.c'
    '*/main/display/widgets/odometer_display.c'
    '*/main/display/widgets/trip_display.c'
    '*/main/display/widgets/warning_lights.c'
    '*/main/display/widgets/tach_arc.c'
)

BUILD_DIR=build-cov

rm -rf "$BUILD_DIR"
cmake -B "$BUILD_DIR" -S . -DCOVERAGE=ON -DCMAKE_BUILD_TYPE=Debug >/dev/null
cmake --build "$BUILD_DIR" --parallel >/dev/null
ctest --test-dir "$BUILD_DIR" --output-on-failure

LCOV_OPTS=(--rc branch_coverage=1 --gcov-tool "$GCOV_TOOL")

lcov "${LCOV_OPTS[@]}" --capture --directory "$BUILD_DIR" \
     --output-file "$BUILD_DIR/coverage.info" >/dev/null

lcov "${LCOV_OPTS[@]}" --extract "$BUILD_DIR/coverage.info" "${SCOPED[@]}" \
     --output-file "$BUILD_DIR/coverage.tested.info" >/dev/null

echo
echo "=== Coverage on tested-by-policy modules ==="
lcov "${LCOV_OPTS[@]}" --summary "$BUILD_DIR/coverage.tested.info"

genhtml --branch-coverage "$BUILD_DIR/coverage.tested.info" \
        --output-directory "$BUILD_DIR/html" >/dev/null

REPORT="$BUILD_DIR/html/index.html"
echo
echo "HTML report: $PWD/$REPORT"

if [[ "${1:-}" != "--no-open" ]] && command -v open >/dev/null 2>&1; then
    open "$REPORT"
fi
