#!/bin/bash
# ---------------------------------------------------------------------------
# Kaempf CP/M 2.2 on the Xebec -- debug run.
#
# Same machine as run-3-kaempf-xebec.command, but with Xebec command and port
# tracing on (-io 0x30) and everything logged to logs/. This is the run to use
# when you want to see what CONFIG and WNFORMAT actually ask the controller to
# do, DCB by DCB.
#
#   ./run-xebec-debug.command          # keep the current work image
#   ./run-xebec-debug.command fresh    # start again from the pristine blank
#
# The work image is a COPY; HDV/g3s-kaempf-cpm22-BLANK.hdv is never written to,
# and is created here if it does not exist, so "fresh" always gives an
# identical starting point -- a blank 10 MB drive,
# 321 cyl x 4 heads x 32 sec x 256 B = 41088 sectors = 1284 blocks of 8 KB.
# That is the geometry this CBIOS sends the controller at boot (INITIALIZE
# DRIVE CHARACTERISTICS: 321 cylinders, 4 heads), so the drive the guest thinks
# it has and the image it gets are the same size.
#
# The Winchester sequence is:
#
#   1. CONFIG     set the drive parameters, including how the Winchester is
#                 split into "Winchesterteile"
#   2. reboot     F10 = soft reset, Shift-F10 = hard reset   <-- REQUIRED
#   3. WNFORMAT   format the Winchester (FINDBAD marks bad blocks afterwards,
#                 PDRIVE reports what the system thinks the geometry is)
#
# Skipping step 2 leaves the system with no partitioning loaded.
# ---------------------------------------------------------------------------

set -e
cd "$(dirname "$0")"
REPO="$(pwd)"
BIN="$REPO/build/sdl2trs"

ROM="$REPO/ROM/g3s_8501004_bootrom_2732.bin"
FLOPPY="$REPO/dmk-working/g3s-kaempf-cpm22.dmk"
BLANK="$REPO/HDV/g3s-kaempf-cpm22-BLANK.hdv"
WORK="$REPO/HDV/g3s-kaempf-cpm22-work.hdv"
LOG_DIR="$REPO/logs"
LOG_FILE="$LOG_DIR/kaempf-xebec-$(date +%Y%m%d-%H%M%S).log"

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

for f in "$ROM" "$FLOPPY"; do
  [ -f "$f" ] || die "Missing file: $f"
done

# Make the pristine blank if it is not there. Cylinders and heads are what the
# CBIOS itself declares at boot with INITIALIZE DRIVE CHARACTERISTICS; SECS is
# the one assumption -- the S1410's usual 32 sectors of 256 B per track. Change
# SECS if that turns out to be wrong; addressing is flat LBA, so all that
# really matters is the total sector count.
if [ ! -f "$BLANK" ]; then
  echo "Creating $BLANK ..."
  CYLS=321 HEADS=4 SECS=32 SECSIZE=256 python3 -c '
import os, sys
cyls, heads, secs, secsize = (int(os.environ[k]) for k in ("CYLS","HEADS","SECS","SECSIZE"))
h = bytearray(256)
h[0], h[1], h[2] = 0x56, 0xCB, 0x10     # Reed magic, format version 1.0
h[4], h[5] = 1, 4                       # one header block
h[10], h[11] = 0x42, 2                  # created by mkdisk; DOS type CP/M
h[26] = heads
h[27], h[28] = (cyls >> 8) & 0xFF, cyls & 0xFF
h[29] = heads * secs                    # sectors per cylinder
h[31] = 1
h[32:64] = b"XEBEC 10MB 321x4x32".ljust(32, b"\0")
h[3] = (sum(h[i] for i in range(32) if i != 3) & 0xFF) ^ 0x4C
total = cyls * heads * secs
open(sys.argv[1], "wb").write(bytes(h) + b"\0" * (total * secsize))
print("  %d cyl x %d heads x %d sec x %d B = %d sectors, %d blocks of 8 KB"
      % (cyls, heads, secs, secsize, total, total * secsize // 8192))
' "$BLANK" || die "could not create $BLANK"
fi

if [ "$1" = "fresh" ] || [ ! -f "$WORK" ]; then
  cp "$BLANK" "$WORK"
  echo "Work image reset from the pristine blank."
fi

mkdir -p "$LOG_DIR"

# Isolated config, as the other launchers do, so ~/.sdltrs.t8c is not read and
# its stale archive paths do not show up as open errors.
CFG="$REPO/run-kaempf-xebec-debug.t8c"
cat > "$CFG" <<EOF
model=1
romfile1=$ROM
EOF

echo "== Kaempf CP/M 2.2 -- Xebec, DEBUG =="
echo "  ROM:    $ROM"
echo "  floppy: $FLOPPY"
echo "  hard0:  $WORK"
echo "  log:    $LOG_FILE"
echo
echo "Winchester sequence:  CONFIG  ->  F10 reboot  ->  WNFORMAT"
echo "Skipping the reboot is the usual reason the drive stays unusable."
echo
echo "Every Xebec command is traced with its full DCB. Quit when done, then"
echo "the log above has the whole conversation."
echo

# -io 0x30 = XEBECDEBUG1 (port level) | XEBECDEBUG2 (commands + full DCB).
# Use 0x20 alone for just the command stream. No -zbx: the trace goes to
# stderr on its own, and -zbx would stop at a debugger prompt instead of
# booting.
"$BIN" "$CFG" \
  -disk0 "$FLOPPY" -disk1 "" -disk2 "" -disk3 "" \
  -disk4 "" -disk5 "" -disk6 "" -disk7 "" \
  -hard0 "$WORK" -hard1 "" -hard2 "" -hard3 "" \
  -io 0x30 \
  -nofullscreen 2>&1 | tee "$LOG_FILE"

echo
echo "sdl2trs exited. Log: $LOG_FILE"
echo
echo "Quick summary of what the controller was asked to do:"
grep -oE "command 0x[0-9A-Fa-f]+" "$LOG_FILE" | sort | uniq -c | sort -rn || true
grep -c "FAILED" "$LOG_FILE" 2>/dev/null | sed 's/^/failed commands: /' || true
[ -t 0 ] && read -n 1 -s -r -p "Press any key to close..."
exit 0
