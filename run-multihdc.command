#!/bin/bash
# Double-click this in Finder (or run from a terminal) to launch this repo's
# own sdl2trs and boot GDOS 2.4 with the Xebec controller attached, for
# testing drives 5/6 (the 0x00-0x02 SASI path). It builds first if needed.
#
# It uses its OWN config file (run-multihdc.t8c, regenerated each run) so
# your global ~/.sdltrs.t8c is left completely alone -- no stale disk/ROM
# paths leaking in. Every slot is also passed explicitly on the command line.
#
# Edit the three paths below if you want a different ROM / boot floppy /
# hard-disk image.

set -e
cd "$(dirname "$0")"
REPO="$(pwd)"
BIN="$REPO/build/sdl2trs"

# --- edit these if needed ---------------------------------------------------
ROM="$REPO/ROM/g3s_8501004_bootrom_2732.bin"      # standard Genie IIIs boot ROM
FLOPPY="$REPO/dmk-working/G3S-GDOS24.DMK"          # GDOS 2.4 bootable system floppy
XEBEC_HDV="$REPO/HDV/g3s-gdos24-omti-10mb.hdv"     # hard-disk image on the Xebec
# ---------------------------------------------------------------------------

# Build if the binary is missing.
if [ ! -x "$BIN" ]; then
  echo "sdl2trs not built yet -- building ..."
  cmake -S "$REPO" -B "$REPO/build" && cmake --build "$REPO/build"
  echo
fi

# Sanity-check the assets so a typo gives a clear message, not a silent boot.
for f in "$ROM" "$FLOPPY" "$XEBEC_HDV"; do
  if [ ! -f "$f" ]; then
    echo "Missing file: $f"
    echo "Edit the path near the top of this script."
    read -n 1 -s -r -p "Press any key to close..."
    exit 1
  fi
done

# Fresh isolated config -- passing a .t8c on the command line makes sdl2trs
# use THIS file instead of ~/.sdltrs.t8c.
CFG="$REPO/run-multihdc.t8c"
cat > "$CFG" <<EOF
model=1
romfile1=$ROM
hardcontroller=xebec
EOF

echo "Booting GDOS 2.4 (Xebec) ..."
echo "  ROM:    $ROM"
echo "  floppy: $FLOPPY"
echo "  xebec0: $XEBEC_HDV"
echo

"$BIN" "$CFG" \
  -disk0 "$FLOPPY" -disk1 "" -disk2 "" -disk3 "" \
  -disk4 "" -disk5 "" -disk6 "" -disk7 "" \
  -xebec0 "$XEBEC_HDV" -xebec1 "" \
  -omti0 "" -omti1 "" \
  -hard0 "" -hard1 "" -hard2 "" -hard3 "" \
  -nofullscreen

echo
echo "Emulator closed."
