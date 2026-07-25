<!-- /CHANGELOG.md -->

# Changelog

## 2026-07-25 — A config setting could switch off attached hard disks (#6)

**The bug.** `hardcontroller=` (and its GUI/CLI equivalents) did not just say
where new images should be attached — it decided which controller was allowed
to answer the bus at all. Every hard-disk port in `trs_io.c` was wrapped in a
`hdctl_get_active() == …` test, eight of them across the `IN` and `OUT` paths,
so the two controllers that were not "active" went silent even with images
attached and a guest driver talking to them.

**Symptom.** A machine that should boot does not, with no diagnostic anywhere:
the guest's driver reads `0xFF` from a controller that is sitting right there
with its image open. Attach an OMTI image and a Xebec image, pin
`hardcontroller=xebec`, and the OMTI — including the Sopp EPROM's boot path —
is dead. Nothing in the UI suggests that a setting about *where images go* has
disabled a disk.

**Why it is wrong.** The three controllers sit at fixed, disjoint port ranges —
WD1000 at `0x50`–`0x57`, OMTI at `0x40`–`0x43`, Xebec at `0x00`–`0x02` on the
TCS onboard SASI adapter — and are wired in independently of each other and of
the boot EPROM. Nothing in the machine arbitrates between them, and neither
EPROM selects one: the standard `g3s_8501004` only ever talks to floppies, and
Sopp's `g3s_hd-omti` boots the OMTI and then falls back to floppy.

A real Genie IIIs was fitted with exactly one hard-disk controller, and the
operating system on the disk decides which one it talks to. Making the *user*
declare it as well is redundant — the OS has already made the choice, and it
never asks the emulator's opinion.

The one thing the selector did buy was enforcing that "exactly one card is
fitted", and without it two controllers can now answer at once, which no real
machine could do. That turns out not to matter, because no Genie IIIs software
looks across the ranges to notice: GDOS 2.4 polls Xebec status at `0x01` and
has no OMTI support at all, Holte's driver drives `0x40`–`0x42`, and Sopp's
EPROM reads `0x42` at reset for its card-presence signature. Every one of those
is a check *within* one controller's range. So "exactly one card fitted" is
carried perfectly well by "exactly one image attached" — which is the user's
own act of configuration, and the thing the guest can actually detect.

**Fix.** All eight guards are gone; each port range dispatches straight to its
controller, in both the `IN` and `OUT` paths. Presence of an image, which the
hardware models as "is the card fitted", is the only thing that decides whether
a controller responds.

Nothing was needed to keep absent controllers quiet — all three backends
already track a `state.present` flag ("at least one image attached") and read
back `0xFF` when it is clear. The write paths did not honour it, so
`trs_hard_out()`, `trs_omti_out()` and `trs_xebec_tcs_out()` now return early
on `!present` too, making an unfitted card inert in both directions rather than
silently advancing a state machine nobody can read.

`hdctl_set_active()` / `hdctl_get_active()` stay, and `hardcontroller=` still
works — but they are now only what they always should have been: the default
for which controller the GUI and CLI attach images to. `trs_hdctl.h` says so.

Reproduction, measured before and after on the same configuration — Sopp EPROM,
an OMTI image and a Xebec image attached, `hardcontroller=xebec` pinned:

| | OMTI port operations | boots |
| --- | --- | --- |
| before | 0 | no |
| after | 190809 (216 × READ `0x08`) | yes |

Regression-checked: scenario 2 still boots straight off the OMTI to `C>` (Holte
BIOS, CP/M V3.0 loader, both ST 225 partitions), and GDOS 2.4's driver still
selects the Xebec on scenario 1 and gets DCB-ready status `0x0B` back.

## 2026-07-25 — Kaempf CP/M retired as a test scenario

Scenario 3 is gone, with its disks, images and launchers. Kämpf's CP/M 3.0 disk
carries no Winchester init utility, so a drive can never be brought up from it —
only `FORMAT`, on an image partitioned somewhere else.

His CP/M 2.2X disks are not a substitute: `cpm22x-g3.dmk` and friends are
**Genie III**, a different machine. Booted here they come up as `EG3200 Genie III
64 KB`, and the TCS SASI ports `0x00`–`0x02` the Xebec lives on are decoded only
under `GENIE3S` in `trs_io.c` — so that machine has no Xebec at all. (It also
needs the 5100-01 system ROM rather than the 8501004 boot EPROM: the two load
the boot sector to 0x4200 and 0xFC00 respectively, and the sector reads its CRTC
table from an absolute 0x4285. With the wrong one the window comes up a few
pixels high with no text.)

Xebec format work therefore stays on GDOS 2.4 (scenario 1), which is the path
that was verified in #7 anyway.

## 2026-07-25 — Kaempf CP/M 3.0 Winchester format fails (#7)

Two independent defects in `trs_xebec.c`, both on the format path.

**Unimplemented opcode.** `CHECK TRACK FORMAT` (Class 0, opcode `0x05`) had no case in `xebec_command()`, so Kämpf's formatter got `unknown command 0x05` and an error status. Added, along with the other Class-0 commands the S1410A manual defines but the emulator lacked: `FORMAT BAD TRACK` (`0x07`), `READ VERIFY` (`0x09`), `READ ECC BURST ERROR LENGTH` (`0x0D`) and `READ SECTOR BUFFER` at its documented `0x10`. (`TRS_XEBEC_READ_BUFFER` stays at `0x0E` — the manual gives `0x0E` to `FORMAT ALTERNATE TRACK`, but `0x0E` is what GDOS 2.4's driver was observed to issue; `0x10` is now accepted as a synonym rather than resolving the discrepancy blind.)

**Format wrote one sector, not one track.** `FORMAT DRIVE` and `FORMAT TRACK` shared a case that seeked to the DCB address, wrote a *single* sector of fill, and reported success. Per the manual, `FORMAT TRACK` formats a whole track and `FORMAT DRIVE` runs to the end of the drive. A guest's "format C:" therefore left 15 of every 16 sectors untouched — visible in `Kaempf CP-M-3-10mb.hdv` as exactly one 256-byte run of `0xE5` per 4096 bytes. New `xebec_format()` fills the whole range.

Supporting fixes:

- **`REQUEST SENSE` reported "no error" unconditionally.** It now returns the latched error code and block address, with the address-valid bit — including the "one block past the last block formatted" convention (manual 4.5.3.4) that a formatter walking the disk track by track reads back.
- **Format is bounded by the image geometry** (`cyls × heads × secs`) and returns `ILLEGAL DISK ADDRESS` (`0x21`) past it. Without this a guest configured for a larger drive than the image walks off the end and `fseek`+`fwrite` silently grow the `.hdv` — which is how `Kaempf CP-M-3-10mb.hdv` reached 100 MB. `READ`/`WRITE` are deliberately left unbounded, so the verified GDOS 2.4 path cannot start seeing address errors it never saw.
- **Format honours write-protect**, failing with `WRITE FAULT` (`0x03`) instead of writing through it.
- **Control-byte bit 5 is honoured**: the host's `WRITE SECTOR BUFFER` contents are used as the format pattern. The default fill stays `0xE5` rather than the manual's `0x6C`, since that is what the working GDOS path has always seen (`TRS_XEBEC_FORMAT_FILL`).

Note the geometry mismatch this exposed: Kämpf's formatter steps **16** sectors per track, while the `.hdv` Reed header declares **17** (68 sectors/cylinder ÷ 4 heads). `xebec_format()` therefore formats `secs` blocks from exactly the requested address instead of rounding down to a track boundary as the manual describes — rounding to *our* boundary would re-format block 0 forever instead of advancing.

## 2026-07-24 — Port mapping wrong (#5)

The emulator was incorrectly letting the Xebec S1410 respond on ports 0x40–0x42 (a leftover hypothesis that Holte's CP/M driver drove the Xebec at OMTI's ports). On the real Genie IIIs, 0x40–0x43 is OMTI-only, and the Xebec is reached solely through the TCS onboard SASI adapter at 0x00–0x02 (the same interface GDOS 2.4 — and, it turns out, Holte's CP/M port with the original EPROM — actually use).

`src/trs_io.c`:

```c
// BEFORE — Xebec wrongly answered at 0x40–0x42
case 0x40: case 0x41: case 0x42:
    if (hdctl_get_active() == XEBEC_DRIVE)
        trs_xebec_out(port, value);
    else if (hdctl_get_active() == OMTI_DRIVE)
        trs_omti_out(port, value);
    break;

// AFTER — 0x40–0x43 is OMTI only; Xebec lives at 0x00–0x02
case 0x40: case 0x41: case 0x42:
    if (hdctl_get_active() == OMTI_DRIVE)
        trs_omti_out(port, value);
    break;
```

The dead Xebec "Holte adapter" code (trs_xebec_in/trs_xebec_out and the 0x40-range port defines) was removed. Xebec's 0x00–0x02 path (GDOS 2.4 drives 5/6) is unchanged. Net −101/+29 lines across trs_io.c, trs_xebec.c, trs_xebec.h.

## 2026-07-22 — Hard-disk backend unification & single active controller

### Added

- `trs_hard_image.c/.h`: shared Reed-header `.hdv` open, geometry decode and
  sector-offset math, previously copy-pasted in `trs_hard.c` (WD1000), `trs_omti.c`
  (OMTI 5527) and `trs_xebec.c` (Xebec S1410). The three backends now keep only
  their own controller state machines.
- `trs_hdctl.c/.h`: a `(controller-type, unit)` dispatch over the three backends,
  plus a single **active controller** setting (`hdctl_get_active` / `_set_active`).
  A real Genie IIIs is fitted with exactly one controller; the active one is chosen
  explicitly (GUI / CLI / config) or auto-resolved from attached images
  (Xebec → OMTI → WD1000) when unset.
- Hard Disk Management menu: a **Controller** selector (GENIE3S only) toggling
  WD1000 / OMTI / Xebec, with generic drive slots for whichever is active.
- Config key `hardcontroller = wd1000|omti|xebec`; CLI flags `-hard2`/`-hard3`,
  `-omti1`, `-xebec1` for the newly addressable drives.

### Changed

- Only the active controller answers on the GENIE3S bus. `trs_io.c` now gates its
  hard-disk port dispatch on `hdctl_get_active()` instead of the old
  image-presence `xebec_active()` heuristic; the other controllers are inert.
- Drive caps raised to each controller's real maximum: WD1000 = 4 (2-bit SDH drive
  field), OMTI / Xebec = 2 (1-bit SASI LUN).
- State-save version is now 16 (also saves the active controller and the wider
  drive arrays).

### Fixed

- "Create Hard Disk Image → Insert into Drive" only ever attached to WD1000 slots,
  whichever slot was picked; it now targets the chosen controller.
- The Space-key write-protect toggle silently ignored OMTI/Xebec rows (the handler
  guarded on `type < ENTRY`, excluding them, and passed the wrong drive index) and
  `trs_write_protect` had no OMTI/Xebec cases at all. Both fixed via the dispatch.

## 2026-07-22 — GDOS 2.4 hard-disk support (XEBEC/TCS host adapter)

### Fixed

- GDOS 2.4 hard-disk access, now working end-to-end (format, directory, read, write). GDOS talks to its hard disk on I/O ports 0x00–0x02, not the 0x40–0x42 ports used by Holte's CP/M-era adapter. The emulator now answers on 0x00–0x02, so GDOS's boot-time probe succeeds and the drive is usable.

### Added

- A second host-adapter interface in `trs_xebec.c` (`trs_xebec_tcs_in` / `trs_xebec_tcs_out`) at ports 0x00–0x02, driving the same controller core as the existing Holte-era path. Sectors are 256 bytes on this path; 512 bytes remains on Holte's 0x40–0x42 path.
- CDB block-count support and the `FORMAT TRACK` / sector-buffer opcodes GDOS uses.
- Dispatch for the new ports in `trs_io.c`'s GENIE3S block.

### Changed

- State-save version bumped to 16.

### Background

GDOS 2.4's resident hard-disk driver lives in high RAM (F000–F4FF). At boot it runs a SASI selection against the Genie IIIs onboard host adapter: write the controller ID to port 0x00, verify, pulse SEL on port 0x02, then poll port 0x01 for BUSY. With those ports unimplemented it read 0xFF (nothing present) and set error code 0x0F ("Bauteil nicht erreichbar").

The visible symptom was an `IN (0x01)` at PC=F1BB repeating 1024 times in the log — the driver's timeout loop. The two previously unexplained flags (5996h / 440Ch) turned out to be downstream consequences of the probe result, not independent configuration.

The driver was recovered by scripting `zbx` over stdin (`-zbx < script.txt`, with `stop f1bb` / `go` / `pe` / `dis`); the breakpoint fires during the automatic boot-time probe, no interaction needed. Disassembly showed 6-byte SASI Class-0 CDBs (opcodes 00/01/03/04/06/08/0A/0F), 256-byte sectors transferred as single auto-handshaked INIR/OTIR bursts, and status bits identical to Holte's board (both expose raw SASI signals).

A successful selection logs: `in 01=00 → out 00,01 → in 00=01 → out 02 → in 01=0B` (BUSY | REQ | C/D — controller answered).

### Verified

- `HDFORMAT` completes both passes ("Durchgang 1/2") — the FORMAT commands and sector-buffer setup return clean completion statuses.
- `dir 5` and `dir 6` return real directory listings (multi-sector 256-byte reads, correctly addressed). GDOS partitions the single physical Xebec unit into two logical drives with its own geometry table: drive 5 = 40 tracks, drive 6 = 163 tracks.
- Write path: a file copied onto drive 5 is written and reads back via `dir 5` in the same session.

The `*` in `PD 0` is not the hard-disk detection marker. Drives 5 and 6 mount, format, and list directories without it, so the `*` marks something else (likely the boot/system drive or a floppy-specific state).

Confirmed the written bytes persist in the `.hdv` across a reboot, on both drive 5 and drive 6, not just in the sector buffer.