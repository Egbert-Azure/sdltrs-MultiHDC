<!-- /docs/architecture/xebec.md — Xebec S1410 controller emulation -->

# Xebec S1410 controller emulation

This describes `src/trs_xebec.c` / `trs_xebec.h`, the Xebec S1410/S1410A SASI/MFM controller emulation. This is the controller the TCS Genie IIIs' built-in hard disk actually used, and the one G-DOS 2.4 and Klaus Kämpf's CP/M port drive. If you are debugging a G-DOS hard-disk problem, start here.

For how it came to be written, and how the port range was found, see `../archaeology/controller-history.md`. For where the protocol facts came from, see `../archaeology/recovered-source.md`.

## What it emulates

A Xebec S1410 reached through the **TCS onboard SASI host adapter** at Z80 ports `0x00`–`0x02`. That is the only interface — the `0x40`–`0x43` range belongs exclusively to the OMTI (`omti.md`). An earlier build also exposed the Xebec at `0x40`–`0x42` on the theory that a Xebec card could sit in Holte's slot; that was wrong and was removed (commit `b2a8c5a`).

Command opcodes and the status-register bit layout come from the real Xebec S1410A Owner's Manual (`../reference/Xebec S1410A Owner Manual_text.pdf`). The host-adapter side — selection sequence, port assignment, 256-byte sectors — came from a live disassembly of G-DOS 2.4's resident driver, and was cross-checked against Thomas Holte's `hd2.mac`.

## Port map

| Port | Direction | Meaning |
|---|---|---|
| `0x00` (`TRS_XEBEC_TCS_DATA`) | R/W | SASI data bus — selection ID, DCB bytes, sector data, and the two completion-status bytes all pass through here |
| `0x01` (`TRS_XEBEC_TCS_CTRL`) | R | Status register: a real SASI bit-flag byte |
| `0x01` (`TRS_XEBEC_TCS_CTRL`) | W | Bus release / deselect — any write aborts to idle |
| `0x02` (`TRS_XEBEC_TCS_SEL`) | W | SEL strobe |

With no image attached in any slot, `hard_image_present()` is false: reads float `0xFF` and writes are ignored, which is what an empty machine does.

## Status register: real bit flags, not phase codes

This is the first structural difference from the OMTI backend, which used one magic value per phase. Port `0x01` here carries genuine SASI signal bits, and C/D together with I/O select the phase:

| Bit | Name | Meaning |
|---|---|---|
| `0x01` | `REQ` | A byte is ready to transfer |
| `0x02` | `BUSY` | Controller selected, mid-command |
| `0x08` | `C/D` | Set during command and status phases |
| `0x10` | `I/O` | Set when the controller is driving the bus (data-in, status) |

The composite values the state machine presents:

| Phase | Status | Bits |
|---|---|---|
| `XEBEC_PH_IDLE` | `0x00` | — |
| `XEBEC_PH_DCB` | `0x0B` | BUSY \| REQ \| C/D |
| `XEBEC_PH_DATA_OUT` | `0x03` | BUSY \| REQ |
| `XEBEC_PH_DATA_IN` | `0x13` | BUSY \| REQ \| I/O |
| `XEBEC_PH_STATUS` | `0x1B` | BUSY \| REQ \| C/D \| I/O |

`0x0B` is the byte to look for in a trace: it is the controller answering a successful selection.

## Selection

Selection is done the real SASI way, not with a bare strobe:

```text
in  01     → wait for BUSY clear
out 00,01  → write controller ID 0x01 to the data bus
in  00     → read it back to verify the bus
out 02     → pulse SEL
in  01=0B  → controller has asserted BUSY, C/D and REQ
```

The emulator responds to SEL only when the latched bus byte has bit 0 set — controller ID 0, the only ID G-DOS's driver ever selects. On success it enters the DCB phase. (Sector size used to be [re-]set here too; that was a bug — see "Geometry and sector size" below.)

## Phases

```text
XEBEC_PH_IDLE
    │  SEL strobe (write to 0x02) with ID 0 latched on 0x00
    ▼
XEBEC_PH_DCB          — 6 DCB bytes written one at a time to 0x00
    │                   6th byte → xebec_command() decodes and dispatches
    ▼
XEBEC_PH_DATA_IN  or  XEBEC_PH_DATA_OUT     (or neither)
    │                   256-byte sectors, moved as one auto-handshaked
    │                   INIR/OTIR burst through 0x00
    ▼
XEBEC_PH_STATUS       — two bytes read at 0x00: the error/LUN byte, then a
    │                   zero byte meaning "done". Reading the second returns
    ▼                   the bus to idle.
XEBEC_PH_IDLE
```

A write to port `0x01` at any point releases the bus and resets to idle.

## The Device Control Block (DCB)

Six bytes, decoded in `xebec_command()`. Byte 0's top three bits are the command class; classes 1–6 are reserved, so for the class-0 commands emulated here byte 0 is the opcode directly.

| Byte | Contents |
|---|---|
| 0 | Opcode |
| 1 | bit 5 = LUN; bits 0–4 = logical block address bits 20–16 |
| 2 | LBA bits 15–8 |
| 3 | LBA bits 7–0 |
| 4 | Block count for READ/WRITE (0 means 256); interleave for the format commands |
| 5 | Control byte; bit 5 = use the host-supplied sector buffer as the format fill pattern |

**Addressing is a flat 21-bit logical block number**, not the OMTI's raw cylinder/head/sector. `xebec_seek()` divides it down through the image's geometry:

```c
sector = lba % d->secs;
cyl    = lba / d->secs;
head   = cyl % d->heads;
cyl    = cyl / d->heads;
```

The LUN is one bit wide, so a guest can address unit 1 even though only `TRS_XEBEC_MAXDRIVES` (2) units exist. An out-of-range LUN is failed as `NOT READY` rather than indexing `hard_slot[]` out of bounds.

## Command set

| Opcode | Name | Notes |
|---|---|---|
| `0x00` | `TEST DRIVE READY` | Opens the image if not already open |
| `0x01` | `RECALIBRATE` | Seek to block 0 |
| `0x03` | `REQUEST SENSE` | 4-byte reply: error code + address-valid bit, then the block in error |
| `0x04` | `FORMAT DRIVE` | Formats from the starting track to the end of the disk |
| `0x05` | `CHECK TRACK` | A flat `.hdv` has no ID fields and cannot be unformatted, so any in-range track passes |
| `0x06` | `FORMAT TRACK` | One track |
| `0x07` | `FORMAT BAD TRACK` | Same as `FORMAT TRACK` — the bad-track flag lives in an ID field a flat image has no room for |
| `0x08` | `READ` | Multi-sector via block count; the data phase stays open and the buffer refills between sectors |
| `0x09` | `READ VERIFY` | READ with no data passed to the host. Deliberately **not** range-checked, because READ and WRITE are not either |
| `0x0A` | `WRITE` | Multi-sector, same way |
| `0x0B` | `SEEK` | Position only |
| `0x0C` | `INITIALIZE DRIVE CHARACTERISTICS` | 8-byte payload consumed and acknowledged; does not change addressing geometry |
| `0x0D` | `READ ECC BURST LENGTH` | Always zero — nothing is ever ECC-corrected here |
| `0x0E` | `READ SECTOR BUFFER` | See the opcode note below |
| `0x0F` | `WRITE SECTOR BUFFER` | Stages the format fill pattern |
| `0x10` | `READ SECTOR BUFFER` | Manual-correct synonym for `0x0E` |

Anything else logs `trs_xebec: unknown command 0x%02X` and fails with `INVALID COMMAND` (`0x20`).

**The `0x0E` / `0x10` discrepancy is deliberate and unresolved.** The S1410A manual numbers the pair `0x0F` WRITE SECTOR BUFFER / `0x10` READ SECTOR BUFFER and gives `0x0E` to FORMAT ALTERNATE TRACK. `TRS_XEBEC_READ_BUFFER` stays at `0x0E` because that is what G-DOS 2.4's driver was observed to issue and what the verified-working path uses; `0x10` is accepted as a synonym so book-correct drivers also work. Get an `XEBECDEBUG2` trace of a real G-DOS session before changing `0x0E`.

## Sense data

Sense describes the command that just completed, so every command except `REQUEST SENSE` clears it first. The codes this emulator can actually produce:

| Code | Meaning |
|---|---|
| `0x00` | No error |
| `0x03` | Write fault — used here for a write-protected image |
| `0x04` | Drive not ready |
| `0x1A` | Format error |
| `0x20` | Invalid command |
| `0x21` | Illegal disk address |

## Geometry and sector size

Geometry is read from the `.hdv` Reed header at open time and never changed. `INITIALIZE DRIVE CHARACTERISTICS` is acknowledged but ignored for addressing — the same decision, for the same reason, as the OMTI's `SET CHARACTERISTICS`; see `omti.md`. It is worth tracing though: a guest declaring a bigger drive than the image is exactly what walks a formatter off the end of the file, and `XEBECDEBUG2` prints the declared geometry alongside the image's.

There is no live sector-size register as on the WD1000: real S1410A hardware fixes sector size with a 256/512 jumper, so this emulator does the same, with `trs_xebec_secsize` (default `TRS_XEBEC_TCS_SECSIZE` = 256, the size G-DOS runs the S1410 at) read once at reset and settable via `-xebecsecsize`. It used to be re-set on every SEL strobe, which made the jumper unemulatable — see "Selection" above.

The format commands fill with `0xE5` (`TRS_XEBEC_FORMAT_FILL`) rather than the manual's documented `0x6C`: `0xE5` is what the verified-working G-DOS `HDFORMAT` path has always seen, and it is also what CP/M expects in an erased directory. Control-byte bit 5 overrides it with the host's staged sector buffer.

## Write protection

The write path does not check the flag itself. A protected image is opened read-only by `hard_image_open()`, so `fwrite()` fails and the command completes with an error. `xebec_format()` does check `d->writeprot` explicitly, and returns `WRITE FAULT` before touching the file.

## Key functions

- `trs_xebec_init(poweron)` — power-on/reset, and also the bus-release path from a write to port `0x01`.
- `trs_xebec_attach(drive, filename)` — slot bookkeeping, called from `trs_hdctl.c`, not directly. (There is no `trs_xebec_remove()`: removal had no backend-specific state to clean up, so `hdctl_remove()` goes straight to `hard_slot_remove()` — see #19.)
- `trs_xebec_tcs_in(port)` / `trs_xebec_tcs_out(port, value)` — the I/O trap entry points, dispatched from `trs_io.c`'s `GENIE3S` block.
- `xebec_command()` — decodes a completed DCB and dispatches.
- `xebec_seek(lun, lba)` — flat LBA to CHS, then `fseek()` through `hard_image_offset()`.
- `xebec_data_in()` / `xebec_data_out(value)` — per-byte data phase, multi-sector continuation, and the two-byte status readback.
- `xebec_format(lun, lba, to_end_of_drive)` — shared by the three format opcodes.
- `xebec_fail(code, addr_valid, lba)` — latch sense and finish with the error bit set.
- `xebec_open(drive)` — wraps `hard_image_open()` with the Xebec's `XEBEC_SEC_PER_TRK` / `XEBEC_MAXHEADS` assumptions.

## Debugging

`-io 0x30` enables `XEBECDEBUG1|2`:

| Flag | Bit | Shows |
|---|---|---|
| `XEBECDEBUG1` | `0x10` | Every port access, with the Z80 PC |
| `XEBECDEBUG2` | `0x20` | Every decoded command, with the whole raw DCB |

The command line prints all six DCB bytes, not just the decoded fields, because byte 4 is the block count for READ/WRITE but the interleave for the format commands, and byte 5 is the control byte — you need both to tell what a guest formatter is really asking for.

```sh
./build/sdl2trs ... -io 0x30 2>&1 | grep "trs_xebec: command\|ERROR"
```

The state-save format for this backend is at version 16.
