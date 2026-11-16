#!/usr/bin/env bash
# Builds every unit test project in test/ and runs it in QEMU (esp32s3).
# test/integration needs real Wi-Fi and Firebase, so it only runs on the board (see README).
#
# Usage: test/run_unit_tests.sh [project ...]   (default: all unit test projects)
set -u

TEST_DIR="$(cd "$(dirname "$0")" && pwd)"
QEMU_TIMEOUT_S=120
SUMMARY_RE='^[0-9]+ Tests [0-9]+ Failures [0-9]+ Ignored'
PASS_RE='^[1-9][0-9]* Tests 0 Failures'

if ! command -v idf.py >/dev/null; then
    echo "ESP-IDF not active: source ~/.espressif/tools/activate_idf_v6.1.sh" >&2
    exit 2
fi

# QEMU never exits on its own: stop it as soon as Unity prints its summary.
run_in_qemu() {
    local project=$1
    local log="$project/build/qemu.log"
    timeout "$QEMU_TIMEOUT_S" idf.py -C "$project" qemu >"$log" 2>&1 &
    local pid=$!
    while kill -0 "$pid" 2>/dev/null && ! grep -qE "$SUMMARY_RE" "$log"; do
        sleep 1
    done
    kill "$pid" 2>/dev/null
    wait "$pid" 2>/dev/null

    grep -E ':FAIL' "$log"
    if ! grep -E "$SUMMARY_RE" "$log"; then
        echo "No Unity summary (crash or timeout), see $log"
        return 1
    fi
    grep -qE "$PASS_RE" "$log"
}

run_project() {
    local project=$1
    mkdir -p "$project/build"
    if ! idf.py -C "$project" build >"$project/build/build.log" 2>&1; then
        echo "Build failed, see $project/build/build.log"
        return 1
    fi
    run_in_qemu "$project"
}

if [ $# -gt 0 ]; then
    projects=("$@")
else
    projects=()
    for dir in "$TEST_DIR"/*/; do
        name=$(basename "$dir")
        [ -f "$dir/CMakeLists.txt" ] && [ "$name" != integration ] && projects+=("$name")
    done
fi

failed=()
for name in "${projects[@]}"; do
    echo "=== $name"
    run_project "$TEST_DIR/$name" || failed+=("$name")
done

echo
if [ ${#failed[@]} -gt 0 ]; then
    echo "FAILED: ${failed[*]}"
    exit 1
fi
echo "All ${#projects[@]} unit test projects passed."
