<!-- /docs/archaeology/recovered-source.md -->

# Where the protocol knowledge came from

None of the three controllers in this emulator was implemented from a complete, authoritative specification. This is the provenance record: for each protocol fact, what it was derived from, and how much weight that source carries. It matters because the sources disagree in places, and when they do, the code follows the one that makes real 1980s software work — not always the one that is documented.

## The sources, ranked

| Source | Kind | Covers |
|---|---|---|
| Xebec S1410A Owner's Manual | Primary vendor document | Xebec command set, DCB layout, status bits, sense codes |
| Xebec SASI/MFM S1510A signal definitions | Primary vendor document | The SASI bus lines themselves — REQ, BSY, C/D, I/O, MSG |
| Thomas Holte's CP/M 3.0 sources | Real, working driver source | OMTI protocol; cross-check for the Xebec |
| Arnulf Sopp's HD-boot EPROM | Binary, disassembled | OMTI port addresses; boot-time behaviour |
| G-DOS 2.4's resident driver | Binary in RAM, disassembled live | The TCS onboard SASI adapter — ports, selection, sector size |
| Volker Dose's `HDNDF.Z80` | Real format utility | OMTI `WRITE SECTOR BUFFER` / `FORMAT` interaction |
| Tim Mann's xtrs | Upstream emulator code | WD1000/1010 — itself inferred, not from a data sheet |

Both vendor documents are in [`../reference/`](../reference/). The Holte and Sopp material lives in the companion archive repositories, not here — see the bottom of this page.

## Holte's CP/M 3.0 sources

The largest single source, and the reason the OMTI emulation exists at all. Fritz Chwolka tracked Thomas Holte down at a new address in the early 1990s, and Holte supplied not just CP/M 3.0 for the Genie IIIs but the **complete sources** — the operating-system components *and* every utility he had written for the machine. The story is told first-hand in [`../reference/Installation des Holte CPM-Plus.md`](../reference/Installation%20des%20Holte%20CPM-Plus.md) (German, Egbert Schröer and Volker Dose, April 1993), which is also the account of fitting an OMTI controller and a 20 MB drive to a real machine.

The files that mattered for this emulator:

| File | What it gave |
|---|---|
| `hd2.mac` | The OMTI BIOS driver. The entire OMTI phase machine, CDB layout and command opcodes were read out of it. Also `MAXDRIVE`, and the `REQMASK`/`BUSYMASK`/`CDMASK`/`IOMASK` equates the Xebec status bits were checked against |
| `ldrbiohd.mac` | The hard-disk boot loader — how a boot actually sequences |
| `DISKIO1.MAC` | `DPBHD1`/`DPBHD2`: how one physical drive is split into CP/M's C: and D: at two cylinder offsets. This is why one `.hdv` is one drive holding two logical volumes |

**There is no official OMTI 5527 documentation behind any of this.** The OMTI protocol as emulated is what Holte's driver does, plus what the boot EPROM does, and nothing else. Where a real OMTI 5527 might behave differently in some corner the driver never exercises, this emulator does not know and does not claim to.

Two consequences are worth calling out because they look like bugs and are not:

- **The CDB always carries head 0.** The compiled driver lets the sector field run past the drive's real sectors-per-track and expects the controller to split head from sector itself. `omti_seek()` replicates that. Do not "fix" it to expect a pre-split head/sector.
- **The boot EPROM sends a stale, wrong drive-characteristics block at every startup.** An early version let `SET CHARACTERISTICS` reprogram the live geometry, which is what the command looks like it is for. Every subsequent seek was corrupted. Geometry is now keyed to the image's Reed header only, in all three backends. See [`design-decisions.md`](design-decisions.md).

## Sopp's HD-boot EPROM

Arnulf Sopp's 1986 retrofit doubled the standard 4 KB 2732 to an 8 KB 2764 to make room for hard-disk boot code. Disassembling it yielded the OMTI port addresses `0x40`–`0x43` and the `0xFA` card-presence signature the ROM checks at reset — a value `hd2.mac` itself never reads. What the two EPROMs do and do not reach is documented separately in [`original-rom-behavior.md`](original-rom-behavior.md).

## G-DOS 2.4's driver: recovered from RAM

This one had no source at all. G-DOS 2.4 keeps its resident hard-disk driver in high RAM at `F000`–`F4FF` and probes for the controller automatically at boot. The probe was failing, and nothing in the OMTI-era code explained why.

The method was to script the `zbx` debugger over stdin rather than drive it interactively:

```sh
sdl2trs … -zbx < script.txt
```

with `stop f1bb` / `go` / `pe f000,f7ff` / `dis`. The breakpoint fires during the boot-time probe with no keyboard interaction at all, which sidestepped the fact that interactive `zbx` entry has never worked on this Mac.

What the disassembly established:

- The selection routine at `F1B6` polls port **`0x01`**, not `0x41`. G-DOS was knocking on a different door — the TCS onboard SASI adapter, not Holte's card slot.
- Selection is the real SASI sequence: wait for BSY clear, write controller ID `0x01` to port `0x00`, **read it back to verify**, pulse SEL on port `0x02`, poll `0x01` for BSY.
- Status bits on `0x01` are bit-for-bit identical to Holte's equates.
- CDBs are built at `F1ED`: 6-byte SASI class-0, opcodes `0x00`/`0x01`/`0x03`/`0x04`/`0x06`/`0x08`/`0x0A`/`0x0F`.
- Sectors are **256 bytes**, moved as one auto-handshaked `INIR`/`OTIR` burst.
- The boot probe is selection-only; on success it patches a driver vector at `F018`. The drive table is filled in later by the `PD`/SYS6 path.

It also closed a dead end. Two flag bytes at `5996h` and `440Ch` had absorbed a lot of time as suspected configuration switches. The disassembly showed they are downstream consequences of a successful probe, not independent config — once the probe worked they set themselves. The temporary memory instrumentation used to watch them has been removed.

The narrative version of this hunt is in [`controller-history.md`](controller-history.md).

## Where the vendor documents win, and where they lose

The Xebec manual is the only genuine specification in the set, and the Xebec command opcodes, DCB layout, status bits and sense codes are taken from it directly. Two places where the code deliberately departs from it, both marked in `trs_xebec.h`:

| Manual says | Code does | Why |
|---|---|---|
| `0x10` = READ SECTOR BUFFER, `0x0E` = FORMAT ALTERNATE TRACK | `0x0E` = READ SECTOR BUFFER, with `0x10` accepted as a synonym | `0x0E` is what G-DOS 2.4's driver was observed to issue, and what the verified-working path uses |
| Format fill pattern `0x6C` | `0xE5` | What the working `HDFORMAT` path has always seen; also CP/M's erased-directory byte |

Both are unresolved, not settled. Either would be reopened by an `XEBECDEBUG2` trace of a real G-DOS session showing otherwise.

The WD1000 is the odd one out: it came from upstream xtrs, where Tim Mann's own header note says the definitions were "inferred from various drivers and sketchy documents found in odd corners" and asks whether anyone has a real WD10xx data sheet. That provenance is inherited as-is. See [`../architecture/wd1000.md`](../architecture/wd1000.md).

## The archives

The disk images, EPROM dumps, Holte sources and reverse-engineering notes are not in this repository. They live in two companion archives:

- **[TCS-Trommeschlaeger-Genie-IIIs](https://github.com/Egbert-Azure/TCS-Trommeschlaeger-Genie-IIIs)** — the general Genie IIIs archive: CP/M and Holte material, disks, EPROMs, source, utilities, how-to articles.
- **[TCS-Trommeschlaeger-Genie-IIIs-GDos-2.4](https://github.com/Egbert-Azure/TCS-Trommeschlaeger-Genie-IIIs-GDos-2.4)** — the G-DOS 2.4 archive: system disks, the Sopp hard-disk boot ROM, and the disassembly notes behind the drives-5/6 work.
