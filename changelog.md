<!-- /CHANGELOG.md -->

# Changelog

## 2026-07-25 — One set of hard-disk slots, no controller to pick (#6)

Follow-on from the port-gating fix below. With the gate gone, the Controller
dropdown in Alt-H had nothing left to do but split one machine's disks into
three separate sets of slots — WD1000 `hard0`–`hard3`, OMTI `omti0`/`omti1`,
Xebec `xebec0`/`xebec1` — which is not how the machine works. A real Genie IIIs
has one controller fitted and the OS on the disk addresses it; asking the user
to declare it as well was asking twice for something they cannot get wrong.

**A slot now holds a disk, not a controller.** There is one set of four slots,
the former WD1000 ones. Whichever controller the guest's OS drives is the one
that serves them. The two SASI controllers still reach only slots 0 and 1,
which their own 1-bit LUN enforces without anyone declaring anything.

- `hard_slot[]` in `trs_hard_image.c` is the machine's slot table; all three
  backends address it directly instead of each keeping `HardImage d[]` of
  their own. One image, one open file handle, one geometry.
- The per-backend `present` flags are gone, replaced by `hard_image_present()`
  over that table. A side effect worth having: a disk attached from the GUI now
  takes effect immediately, where before the flag was latched at power-on and
  the controller ignored the disk until the next reset.
- The slot table is saved once, by the layer that owns it, instead of three
  times over by three backends. State version 16 → 17.
- `hdctl_*` lost its controller-type argument throughout; `hdctl_get_active()`
  / `_set_active()`, and the `OMTI_DRIVE` / `XEBEC_DRIVE` menu-row types, are
  gone entirely.
- **Alt-H**: no Controller row, one list of four slots. Slots 2 and 3 are
  tagged `WD1000 only`, since the SASI controllers' 1-bit LUN reaches units 0
  and 1 alone — with the controller gone from the screen, that limit had
  nowhere else to show.
- **CLI/config**: `-hard0`..`-hard3` and `hard0`..`hard3` are the whole story.
  `hardcontroller=` is accepted and ignored so existing `.t8c` files still
  load. `-omti<n>` / `-xebec<n>` still name the same slots, but an *empty*
  value is now ignored rather than clearing the slot — old configs habitually
  set one controller and blanked the others, and under one-slot semantics that
  blanking would wipe the disk that was just attached.

Regression: both controller paths behave exactly as before, byte for byte —
scenario 2 boots off the OMTI with the same 190809 port operations and 216
`READ` commands, and GDOS 2.4 still selects the Xebec and gets `0x0B` back.

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

A real Genie IIIs does have exactly one controller fitted — but the OS on the
disk already decides which one it addresses, so making the user declare it too
was redundant. The only thing the setting bought was modelling "one card", and
no Genie IIIs software can tell: GDOS 2.4 polls Xebec status at `0x01` and has
no OMTI support at all, Holte's driver drives `0x40`–`0x42`, Sopp's EPROM reads
`0x42` for its presence signature. Every one of those checks stays inside a
single controller's range, so "one card fitted" is carried well enough by "a
disk is attached".

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

`hdctl_set_active()` / `hdctl_get_active()` and `hardcontroller=` survived this
step, demoted to naming the default slot for GUI and CLI attachment. They did
not survive the follow-on above, which found they had nothing left to decide.

Reproduction, measured before and after on the same configuration — Sopp EPROM,
an OMTI image and a Xebec image attached, `hardcontroller=xebec` pinned:

| | OMTI port operations | boots |
| --- | --- | --- |
| before | 0 | no |
| after | 190809 (216 × READ `0x08`) | yes |

Regression-checked: scenario 2 still boots straight off the OMTI to `C>` (Holte
BIOS, CP/M V3.0 loader, both ST 225 partitions), and GDOS 2.4's driver still
selects the Xebec on scenario 1 and gets DCB-ready status `0x0B` back.

## 2026-07-25 — Scenario 3 is now Kaempf CP/M 2.2 (Genie IIIs)

Scenario 3 was Kämpf's CP/M **3.0**, which turned out to be useless for
Winchester work: it carries no hard-disk init utility at all, so a drive can
never be brought up from it — only `FORMAT`, on an image partitioned somewhere
else. It is replaced by Klaus Kämpf's **Genie IIIs CP/M 2.2**, CBIOS 2.6 vom
3.3.85 (`dmk-working/g3s-kaempf-cpm22.dmk`), which has the whole toolchain:

```text
CONFIG     drive parameters, including up to seven "Winchesterteile"
<reboot>   F10 soft, Shift-F10 hard   -- required; the partitioning is re-read
WNFORMAT   format the Winchester
FINDBAD    mark bad blocks     PDRIVE   report the geometry in force
```

Its CBIOS already drives the controller during startup — the boot banner's
`Initialisiere Winchester` line is `TEST DRIVE READY` (`0x00`), `RECALIBRATE`
(`0x01`) and `INITIALIZE DRIVE CHARACTERISTICS` (`0x0C`), issued twice as it
probes both LUNs, with the selection handshake reaching status `0x0B`. That is
a good deal more of the S1410 command set than GDOS 2.4 ever exercises, so it
is the better test of `trs_xebec.c`.

**Bringing a drive up from it is unfinished**, so the scenario attaches no
hard-disk image. Two attempts got WNFORMAT to run and report success while
writing *nothing at all* to the image — 0 of 41,088 sectors — but both skipped
`CONFIG`, which is what assigns drive letters to the Winchesterteile, so
neither is evidence against the emulator. `run-xebec-debug.command [fresh]`
picks the work up: it boots the same machine with `-io 0x30` logging every DCB,
and creates the blank drive it needs.

That blank's geometry is not guesswork on the cylinder count: this CBIOS
declares **321 cylinders, 4 heads** to the controller at boot. Over the TCS
adapter the S1410 uses 256-byte sectors, and 32 sectors per track is *assumed*
— giving 321 × 4 × 32 = 41,088 sectors ≈ 10 MB, which addressing treats as a
flat LBA space, so only the total matters. If that assumption is wrong, the
number to change is the sectors-per-track in `run-xebec-debug.command`.

Not to be confused with the `cpm22x` disks in the archive (`cpm22x-g3.dmk` and
friends): those are **Genie III**, a different machine. They come up as `EG3200
Genie III 64 KB`, want the 5100-01 system ROM rather than the 8501004 boot
EPROM, and have no Xebec at all — the TCS SASI ports `0x00`–`0x02` are decoded
only under `GENIE3S` in `trs_io.c`.

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