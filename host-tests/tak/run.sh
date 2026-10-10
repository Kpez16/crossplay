#!/bin/sh
# Builds and runs the Tak rules and opponent tests. TakCore and TakBrain are
# freestanding C++17.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-tak-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/tak
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC test_tak.cpp -o "$BUILD_DIR/test_tak"
"$BUILD_DIR/test_tak"
