#!/bin/bash
# Debug/tracing variant of boot_gdos24_xebec.command: every run logs full
# port-level I/O tracing (every Z80 IN/OUT plus the OMTI/Xebec-specific
# debug output) to logs/, and the zbx debugger is enabled.
#
# Use this instead of the plain script when investigating a problem: do
# whatever fails on screen, then the log in logs/ has the complete port
# conversation for cross-checking afterward.
#
# zbx notes: the F9 hotkey to break in does not work on this Mac, but zbx
# is fully scriptable instead -- run this repo's build by hand with
# "-zbx < script.txt" where the script contains commands like
# "stop f1bb" / "go" / "pe f000,f7ff" / "dis f100,f260" / "quit".
# Breakpoints set that way fire without any keyboard interaction.
#
# Every disk/hard/omti/xebec slot is passed explicitly (empty string to
# clear) so this never boots whatever was last left in ~/.sdltrs.t8c.

set -e
cd "$(dirname "$0")"
REPO="$(pwd)"

ROM_PATH="$REPO/ROM/g3s_8501004_bootrom_2732.bin"
DISK0_PATH="$REPO/dmk-working/G3S-GDOS24.DMK"
XEBEC_HDV="$REPO/HDV/g3s-gdos24-omti-10mb.hdv"
LOG_DIR="$REPO/logs"
LOG_FILE="$LOG_DIR/boot_gdos24_xebec_debug_$(date +%Y%m%d_%H%M%S).log"

if [ ! -x "$REPO/build/sdl2trs" ]; then
  echo "sdl2trs not found or not executable at: $REPO/build/sdl2trs"
  echo "Build it first: mkdir -p build && cd build && cmake .. && cmake --build ."
  read -n 1 -s -r -p "Press any key to close..."
  exit 1
fi

for f in "$ROM_PATH" "$DISK0_PATH" "$XEBEC_HDV"; do
  if [ ! -f "$f" ]; then
    echo "Missing required file: $f"
    read -n 1 -s -r -p "Press any key to close..."
    exit 1
  fi
done

mkdir -p "$LOG_DIR"
echo "Logging to: $LOG_FILE"
echo
echo "=============================================================="
echo "  Debug run: full port-level I/O tracing is being logged to"
echo "  the file above. Do whatever you want to investigate, quit,"
echo "  then check the log in logs/."
echo "=============================================================="
echo

"$REPO/build/sdl2trs" -model 1 \
  -rom "$ROM_PATH" \
  -disk0 "$DISK0_PATH" \
  -disk1 "" -disk2 "" -disk3 "" -disk4 "" -disk5 "" -disk6 "" -disk7 "" \
  -hard0 "" -hard1 "" \
  -omti0 "" \
  -x0 "$XEBEC_HDV" \
  -diskdebug 0x3 -io 0x3f \
  -zbx -fs 2>&1 | tee "$LOG_FILE"

echo
echo "sdl2trs exited. Log saved at: $LOG_FILE"
read -n 1 -s -r -p "Press any key to close..."
