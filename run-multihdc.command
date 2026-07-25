#!/bin/bash
# ---------------------------------------------------------------------------
# sdltrs-MultiHDC test launcher
#
# Boots this repo's own build/sdl2trs in one of the known-good Genie IIIs test
# configurations. Boot EPROM, controller and .hdv are a matched set -- see
# docs/boot-eprom-controller-pairing.md; if they disagree, the disk is simply
# not found.
#
#   1  gdos    GDOS 2.4        standard EPROM  Xebec  g3s-gdos24-xebec-10mb.hdv
#   2  holte   Holte CP/M 3.0  Sopp EPROM      OMTI   g3s-omti-WORKING.hdv
#   4  z3plus  as 2, but on    g3s-omti-Z3Plus.hdv (same system, more tools)
#
# (3 was Kaempf's CP/M; retired -- his CP/M 3.0 disk has no Winchester init
# utility, and the 2.2X "cpm22x" disks are Genie III, a different machine.)
#
# Usage:
#   ./run-multihdc.command          # menu -- this is what a Finder double-click gets
#   ./run-multihdc.command gdos     # or 1 / holte / 2 / z3plus / 4
#
# Each scenario writes its own run-<name>.t8c and passes every disk/hard/omti/
# xebec slot explicitly, so ~/.sdltrs.t8c is never read and nothing stale from
# a previous session leaks in. It builds first if the binary is missing.
# ---------------------------------------------------------------------------

set -e
cd "$(dirname "$0")"
REPO="$(pwd)"
BIN="$REPO/build/sdl2trs"

ROM_STD="$REPO/ROM/g3s_8501004_bootrom_2732.bin"   # standard Genie IIIs boot ROM (2732, 4 KB)
ROM_OMTI="$REPO/ROM/g3s_hd-omti_bootrom_2764.bin"  # Arnulf Sopp's HD-boot ROM (2764, 8 KB)

die() {
  echo
  echo "$1"
  [ -t 0 ] && read -n 1 -s -r -p "Press any key to close..."
  exit 1
}

# --- pick the scenario -----------------------------------------------------
choice="$1"
if [ -z "$choice" ]; then
  echo "sdltrs-MultiHDC -- which test?"
  echo
  echo "  1  GDOS 2.4         standard EPROM   Xebec   drives 5/6"
  echo "  2  Holte CP/M 3.0   Sopp EPROM       OMTI    boots from HD, C: and D:"
  echo "  4  Holte CP/M 3.0   Sopp EPROM       OMTI    Z3Plus image (more tools)"
  echo
  read -r -p "Choice [1]: " choice
  choice="${choice:-1}"
fi

case "$(echo "$choice" | tr '[:upper:]' '[:lower:]')" in
  1|gdos|gdos24)
    NAME="gdos24-xebec"
    LABEL="GDOS 2.4 -- standard EPROM, Xebec controller"
    ROM="$ROM_STD"
    CTL="xebec"
    FLOPPY="$REPO/dmk-working/G3S-GDOS24.DMK"
    HDV="$REPO/HDV/g3s-gdos24-xebec-10mb.hdv"
    HINT="Boots from floppy; GDOS's resident driver then probes the HD at ports
0x00-0x02 and offers it as drives 5 and 6.
Try:  PD 5      (also GENDIR 5 / DIR 5; HDFORMAT, confirm with JA, to reformat)"
    ;;
  2|holte|omti)
    NAME="holte-omti"
    LABEL="Holte CP/M 3.0 -- Sopp HD-boot EPROM, OMTI controller"
    ROM="$ROM_OMTI"
    CTL="omti"
    FLOPPY=""
    HDV="$REPO/HDV/g3s-omti-WORKING.hdv"
    HINT="Boots straight off the hard disk, no floppy: GENIE IIIs banner, CP/M V3.0
loader, RESBIOS3/BNKBIOS3/RESBDOS3/BNKBDOS3, 60K TPA, then C>.
C: and D: are two partitions of this one image (cyl 2 and cyl 307)."
    ;;
  4|z3plus|z3)
    NAME="holte-omti-z3plus"
    LABEL="Holte CP/M 3.0 -- Sopp HD-boot EPROM, OMTI controller (Z3Plus image)"
    ROM="$ROM_OMTI"
    CTL="omti"
    FLOPPY=""
    HDV="$REPO/HDV/g3s-omti-Z3Plus.hdv"
    HINT="Same as test 2, on the Z3Plus image -- same system, more tools installed."
    ;;
  *)
    die "Unknown scenario '$choice' (use 1/gdos, 2/holte, 4/z3plus)."
    ;;
esac

# --- build if needed -------------------------------------------------------
if [ ! -x "$BIN" ]; then
  echo "sdl2trs not built yet -- building ..."
  cmake -S "$REPO" -B "$REPO/build" && cmake --build "$REPO/build"
  echo
fi

# --- sanity-check the assets so a typo gives a message, not a silent boot ---
CHECK=("$ROM" "$HDV")
[ -n "$FLOPPY" ] && CHECK+=("$FLOPPY")
for f in "${CHECK[@]}"; do
  [ -f "$f" ] || die "Missing file: $f"
done

# --- isolated config; passing a .t8c makes sdl2trs ignore ~/.sdltrs.t8c ----
CFG="$REPO/run-$NAME.t8c"
cat > "$CFG" <<EOF
model=1
romfile1=$ROM
hardcontroller=$CTL
EOF

echo "== $LABEL =="
echo "  ROM:        $ROM"
echo "  controller: $CTL"
echo "  floppy0:    ${FLOPPY:-(none -- booting from hard disk)}"
echo "  hard disk:  $HDV"
echo
echo "$HINT"
echo
echo "Alt-D / Alt-F: floppy management   Alt-H: hard-disk management"
echo

# Only the active controller's slot gets the image; every other slot is
# cleared explicitly.
if [ "$CTL" = "xebec" ]; then
  CTL_ARGS=(-xebec0 "$HDV" -xebec1 "" -omti0 "" -omti1 "")
else
  CTL_ARGS=(-omti0 "$HDV" -omti1 "" -xebec0 "" -xebec1 "")
fi

"$BIN" "$CFG" \
  -disk0 "$FLOPPY" -disk1 "" -disk2 "" -disk3 "" \
  -disk4 "" -disk5 "" -disk6 "" -disk7 "" \
  "${CTL_ARGS[@]}" \
  -hard0 "" -hard1 "" -hard2 "" -hard3 "" \
  -nofullscreen

echo
echo "sdl2trs exited."
[ -t 0 ] && read -n 1 -s -r -p "Press any key to close..."
exit 0
