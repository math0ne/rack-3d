#!/usr/bin/env bash
# Builds and runs the rack3d tests against the already-built libRack.
# Run from an MSYS2 MINGW64 shell in the Rack directory, after `make`:  bash tests/rack3d/run.sh
set -euo pipefail
cd "$(dirname "$0")/../.."

mkdir -p build/tests
g++ -std=c++11 -g -O1 -Wall -Wextra -Wno-unused-parameter -D_USE_MATH_DEFINES \
	-Iinclude -Idep/include \
	tests/rack3d/layout_test.cpp \
	-L. -lRack \
	-o build/tests/layout_test.exe

# libRack.dll lives in the Rack directory
PATH="$PWD:$PATH" ./build/tests/layout_test.exe
