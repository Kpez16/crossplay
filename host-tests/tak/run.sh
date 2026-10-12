#!/bin/sh
# Builds and runs the Tak rules and opponent tests. TakCore and TakBrain are
# freestanding C++17. TakFlow (the tap-to-move logic) is tested alongside.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-tak-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/tak
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC test_tak.cpp -o "$BUILD_DIR/test_tak"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC test_flow.cpp -o "$BUILD_DIR/test_flow"
"$BUILD_DIR/test_flow"
"$BUILD_DIR/test_tak"
