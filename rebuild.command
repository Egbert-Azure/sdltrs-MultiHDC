#!/bin/bash
# Double-click this in Finder (or run from a terminal) to (re)compile this
# sdltrs-MultiHDC checkout's own sdl2trs into build/sdl2trs. This build is
# fully isolated to this repository -- nothing is installed system-wide, and
# any other sdltrs checkouts you have are untouched.
#
# After a successful build, test with:
#   run-1-gdos24-xebec.command  -- boot GDOS 2.4 with the Xebec disk
#   run-xebec-debug.command     -- same, with the zbx debugger enabled

set -e
cd "$(dirname "$0")"
REPO="$(pwd)"

mkdir -p "$REPO/build"

echo "Configuring (cmake) ..."
cmake -S "$REPO" -B "$REPO/build"

echo
echo "Building ..."
cmake --build "$REPO/build"

echo
echo "Done: $REPO/build/sdl2trs"
ls -l "$REPO/build/sdl2trs"
echo
echo "Run run-1-gdos24-xebec.command to boot GDOS 2.4 against it."
read -n 1 -s -r -p "Press any key to close..."
echo
