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
#   3  kaempf  Kaempf CP/M 2.2 standard EPROM  Xebec  (floppy only, no HD yet)
#   4  z3plus  as 2, but on    g3s-omti-Z3Plus.hdv (same system, more tools)
#
# Usage:
#   ./run-multihdc.command          # menu -- this is what a Finder double-click gets
#   ./run-multihdc.command gdos     # or 1 / holte / 2 / kaempf / 3 / z3plus / 4
#
# Each scenario writes its own run-<name>.t8c and passes every floppy and
# hard-disk slot explicitly, so ~/.sdltrs.t8c is never read and nothing stale
# from a previous session leaks in. It builds first if the binary is missing.
#
# The controller column is a fact about the disk, not a setting: all three
# controllers answer on their own fixed ports, and the OS on the disk picks
# the one it talks to.
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
  echo "  3  Kaempf CP/M 2.2   standard EPROM   Xebec   boots from floppy"
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
  3|kaempf|kämpf|cpm22)
    NAME="kaempf-xebec"
    LABEL="Kaempf CP/M 2.2 -- standard EPROM, Xebec controller"
    ROM="$ROM_STD"
    CTL="xebec"
    FLOPPY="$REPO/dmk-working/g3s-kaempf-cpm22.dmk"
    HDV=""
    HINT="Klaus Kaempf's Genie IIIs CP/M 2.2 (CBIOS 2.6 vom 3.3.85). Boots from floppy.
Its CBIOS finds and initialises the Xebec at boot over ports 0x00-0x02 -- the
'Initialisiere Winchester' line -- and the disk carries the hard-disk tooling:
CONFIG (drive letters, incl. seven 'Winchesterteile'), WNFORMAT, FINDBAD, PDRIVE.

No hard-disk image is attached: bringing a drive up from here is unfinished work,
and the last attempt never wrote a byte to the image. To pick it up again, make a
blank sized to what this CBIOS declares at boot (INITIALIZE DRIVE CHARACTERISTICS
says 321 cylinders, 4 heads; over the TCS adapter the S1410 uses 256-byte sectors,
so 321 x 4 x 32 = 41088 sectors), attach it with -hard0, and run CONFIG before
WNFORMAT -- the two prior attempts skipped CONFIG.

Note CONFIG saves to drive A:, i.e. it writes to the floppy image itself."
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
    die "Unknown scenario '$choice' (use 1/gdos, 2/holte, 3/kaempf, 4/z3plus)."
    ;;
esac

# --- build if needed -------------------------------------------------------
if [ ! -x "$BIN" ]; then
  echo "sdl2trs not built yet -- building ..."
  cmake -S "$REPO" -B "$REPO/build" && cmake --build "$REPO/build"
  echo
fi

# --- sanity-check the assets so a typo gives a message, not a silent boot ---
CHECK=("$ROM")
[ -n "$FLOPPY" ] && CHECK+=("$FLOPPY")
[ -n "$HDV" ] && CHECK+=("$HDV")
for f in "${CHECK[@]}"; do
  [ -f "$f" ] || die "Missing file: $f"
done

# --- isolated config; passing a .t8c makes sdl2trs ignore ~/.sdltrs.t8c ----
CFG="$REPO/run-$NAME.t8c"
cat > "$CFG" <<EOF
model=1
romfile1=$ROM
EOF

echo "== $LABEL =="
echo "  ROM:        $ROM"
echo "  controller: $CTL (reached by the guest OS; not a setting)"
echo "  floppy0:    ${FLOPPY:-(none -- booting from hard disk)}"
echo "  hard disk:  ${HDV:-(none attached)}"
echo
echo "$HINT"
echo
echo "Alt-D / Alt-F: floppy management   Alt-H: hard-disk management"
echo

# A hard-disk slot holds an image, not a controller: the image goes in
# slot 0 and whichever controller the guest's OS drives serves it.  The
# remaining slots are cleared explicitly so nothing stale leaks in.
"$BIN" "$CFG" \
  -disk0 "$FLOPPY" -disk1 "" -disk2 "" -disk3 "" \
  -disk4 "" -disk5 "" -disk6 "" -disk7 "" \
  -hard0 "$HDV" -hard1 "" -hard2 "" -hard3 "" \
  -nofullscreen

echo
echo "sdl2trs exited."
[ -t 0 ] && read -n 1 -s -r -p "Press any key to close..."
exit 0
