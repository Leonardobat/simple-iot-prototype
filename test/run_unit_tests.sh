#!/usr/bin/env bash
# Builds every unit test project in test/ and runs them in QEMU (esp32s3) with pytest-embedded
# (test/test_unit.py). test/integration needs real Wi-Fi and Firebase, so it only runs on the board
# (make test-integration).
#
# Usage: test/run_unit_tests.sh [project ...]   (default: all unit test projects)
set -u

TEST_DIR="$(cd "$(dirname "$0")" && pwd)"

if ! command -v idf.py >/dev/null || ! command -v pytest >/dev/null; then
    echo "ESP-IDF not active or pytest-embedded missing: source ~/.espressif/tools/activate_idf_v6.1.sh" >&2
    echo "and see the README (pip install pytest-embedded pytest-embedded-idf pytest-embedded-qemu)." >&2
    exit 2
fi

if [ $# -gt 0 ]; then
    projects=("$@")
else
    projects=()
    for dir in "$TEST_DIR"/*/; do
        name=$(basename "$dir")
        [ -f "$dir/CMakeLists.txt" ] && [ "$name" != integration ] && projects+=("$name")
    done
fi

build_failed=()
cases=()
for name in "${projects[@]}"; do
    project="$TEST_DIR/$name"
    mkdir -p "$project/build"
    echo "=== build $name"
    if idf.py -C "$project" build >"$project/build/build.log" 2>&1; then
        cases+=("$TEST_DIR/test_unit.py::test_unit[$name]")
    else
        echo "Build failed, see $project/build/build.log"
        build_failed+=("$name")
    fi
done

status=0
if [ ${#cases[@]} -gt 0 ]; then
    pytest -c "$TEST_DIR/pytest.ini" "${cases[@]}" || status=1
fi

echo
if [ ${#build_failed[@]} -gt 0 ]; then
    echo "BUILD FAILED: ${build_failed[*]}"
    exit 1
fi
[ "$status" -eq 0 ] && echo "All ${#projects[@]} unit test projects passed."
exit "$status"
