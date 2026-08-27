#!/bin/bash
# ---------------------------------------------------------------------------
# Holte CP/M 3.0 on the OMTI -- debug run.
#
# Same machine as run-2-holte-omti.command, but with OMTI command and port
# tracing on (-io 0x0C) and everything logged to logs/. This is the run to
# use when you want to see what CP/M's resident driver actually asks the
# OMTI 5527 to do, phase by phase.
#
#   ./run-omti-debug.command
#
# Boots straight off HDV/g3s-omti-WORKING.hdv (Sopp HD-boot EPROM, no floppy),
# same as run-multihdc.command's scenario 2. That image is used directly and
# is not copied -- this is a debug trace of the working system, not a fresh
# boot test, so there is no BLANK/WORK split here.
# ---------------------------------------------------------------------------

set -e
cd "$(dirname "$0")"
REPO="$(pwd)"
BIN="$REPO/build/sdl2trs"

ROM="$REPO/ROM/g3s_hd-omti_bootrom_2764.bin"
HDV="$REPO/HDV/g3s-omti-WORKING.hdv"
LOG_DIR="$REPO/logs"
LOG_FILE="$LOG_DIR/omti-debug-$(date +%Y%m%d-%H%M%S).log"

die() {
  echo
  echo "$1"
  [ -t 0 ] && read -n 1 -s -r -p "Press any key to close..."
  exit 1
}

if [ ! -x "$BIN" ]; then
  echo "sdl2trs not built yet -- building ..."
  cmake -S "$REPO" -B "$REPO/build" && cmake --build "$REPO/build"
  echo
fi

for f in "$ROM" "$HDV"; do
  [ -f "$f" ] || die "Missing file: $f"
done

mkdir -p "$LOG_DIR"

# Isolated config, as the other launchers do, so ~/.sdltrs.t8c is not read and
# its stale archive paths do not show up as open errors.
CFG="$REPO/run-holte-omti-debug.t8c"
cat > "$CFG" <<EOF
model=1
romfile1=$ROM
EOF

echo "== Holte CP/M 3.0 -- OMTI, DEBUG =="
echo "  ROM:  $ROM"
echo "  hard0: $HDV"
echo "  log:   $LOG_FILE"
echo
echo "Boots straight off the hard disk; no floppy, no reboot sequence needed."
echo
echo "Every OMTI command is traced with its full phase transitions. Quit when"
echo "done, then the log above has the whole conversation."
echo

# -io 0x0C = OMTIDEBUG1 (port level) | OMTIDEBUG2 (commands). No -zbx: the
# trace goes to stderr on its own, and -zbx would stop at a debugger prompt
# instead of booting.
"$BIN" "$CFG" \
  -disk0 "" -disk1 "" -disk2 "" -disk3 "" \
  -disk4 "" -disk5 "" -disk6 "" -disk7 "" \
  -hard0 "$HDV" -hard1 "" -hard2 "" -hard3 "" \
  -io 0x0C \
  -nofullscreen 2>&1 | tee "$LOG_FILE"

echo
echo "sdl2trs exited. Log: $LOG_FILE"
echo
echo "Quick summary of what the controller was asked to do:"
grep -oE "command 0x[0-9A-Fa-f]+" "$LOG_FILE" | sort | uniq -c | sort -rn || true
grep -c "FAILED" "$LOG_FILE" 2>/dev/null | sed 's/^/failed commands: /' || true
[ -t 0 ] && read -n 1 -s -r -p "Press any key to close..."
exit 0
