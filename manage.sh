#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR="$ROOT/bin"

usage() {
    echo "Használat: $0 <parancs>"
    echo ""
    echo "Parancsok:"
    echo "  build      Release build → bin/home-appliances"
    echo "  debug      Debug build ASAN-nal"
    echo "  test       Unit tesztek ASAN-nal"
    echo "  valgrind   Valgrind leak ellenőrzés"
    echo "  coverage   GCOV/LCOV lefedettség"
    echo "  clean      Build artefaktek törlése"
    echo "  deps       Rendszerfüggőségek telepítése"
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
        echo "Build kész: $BIN_DIR/home-appliances"
        ;;
    debug)
        build_type Debug
        echo "Debug build kész: $BIN_DIR/home-appliances"
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
        echo "Lefedettség: $ROOT/build-Coverage/coverage-report/index.html"
        ;;
    clean)
        rm -rf "$ROOT"/build-* "$BIN_DIR"
        echo "Tisztítás kész."
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
