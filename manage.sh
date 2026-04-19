#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$ROOT/bin"

usage() {
    echo "Usage: $0 <command>"
    echo ""
    echo "Commands:"
    echo "  build      Release build → bin/home-appliances"
    echo "  debug      Debug build with ASAN"
    echo "  test       Unit tests with ASAN"
    echo "  valgrind   Valgrind leak check"
    echo "  coverage   GCOV/LCOV coverage report"
    echo "  clean      Remove build artifacts"
    echo "  deps       Install system dependencies"
}

build_type() {
    local type="$1"
    local dir="$ROOT/build-$type"
    cmake -S "$ROOT" -B "$dir" -DCMAKE_BUILD_TYPE="$type" -DCMAKE_VERBOSE_MAKEFILE=OFF
    cmake --build "$dir" --parallel "$(nproc)"
}

cmd="${1:-}"
case "$cmd" in
    build)
        build_type Release
        echo "Build done: $BIN_DIR/home-appliances"
        ;;
    debug)
        build_type Debug
        echo "Debug build done: $BIN_DIR/home-appliances"
        ;;
    test)
        build_type Debug
        cd "$ROOT/build-Debug"
        ctest --output-on-failure
        ;;
    valgrind)
        build_type Release
        valgrind --leak-check=full --error-exitcode=1 "$BIN_DIR/home-appliances" --help
        ;;
    coverage)
        build_type Coverage
        cd "$ROOT/build-Coverage"
        ctest --output-on-failure
        lcov --capture --directory . --output-file coverage.info
        lcov --remove coverage.info '/usr/*' '*/tests/*' --output-file coverage.info
        genhtml coverage.info --output-directory coverage-report
        echo "Coverage report: $ROOT/build-Coverage/coverage-report/index.html"
        ;;
    clean)
        rm -rf "$ROOT"/build-* "$BIN_DIR"
        echo "Clean done."
        ;;
    deps)
        sudo apt-get update
        sudo apt-get install -y cmake gcc valgrind lcov
        ;;
    *)
        usage
        exit 1
        ;;
esac
