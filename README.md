<!-- /README.md — sdltrs-MultiHDC -->

# sdltrs-MultiHDC

A TRS-80 / TCS Genie IIIs emulator that emulates three distinct hard-disk controllers: the OMTI 5527, the WD1000/1010, and the Xebec S1410 SASI. It derives from `sdltrsOMTI`, itself based on [SDL2TRS](https://gitlab.com/jengun/sdltrs) / xtrs. The tree was started fresh rather than forked, so upstream commit history is not preserved here — see the upstream projects for that. Per-file copyright headers and `AUTHORS.md` record which code came from where.

The single remote is `origin` → [`github.com/Egbert-Azure/sdltrs-MultiHDC`](https://github.com/Egbert-Azure/sdltrs-MultiHDC). The GitHub repo was renamed from `sdltrsOMTI` and old URLs auto-redirect. Development happens on `main`.

The fork had two goals, both now working and verified:

1. Boot Holte's CP/M 3.0 from hard disk on the OMTI controller, using Arnulf Sopp's 1986 HD-boot EPROM — the OMTI 5527 path working end to end, from the boot ROM through the CP/M loader.
2. Emulate the Xebec S1410, the controller GDOS 2.4 and Klaus Kaempf's CP/M port for the TCS Genie IIIs actually use. The S1410 is closer to the OMTI 5527 than to the WD1000, but compatible with neither.

Getting there also meant unifying the three hard-disk backends onto shared image handling and controller-free disk slots.

![Holte CP/M 3.0 booting from hard disk on the OMTI controller with Arnulf Sopp's EPROM: BIOS b (851120, Thomas Holte 1985), CP/M V3.0 loader, 60K TPA, two Seagate ST 225 (21.4 MB) drives, C> prompt](images/holte-cpm3-hd-boot.png)

## Status

**OMTI with Holte's CP/M 3.0 from hard disk** (screenshot above): with Sopp's HD-boot EPROM, the OMTI 5527 path boots CP/M 3.0 straight off the hard disk. RESBIOS3 / BNKBIOS3 / RESBDOS3 / BNKBDOS3 load, 60K TPA, both drives recognized, `C>` prompt. See [`docs/guides/HOWTO.md`](docs/guides/HOWTO.md) and [`docs/architecture/omti.md`](docs/architecture/omti.md).

**Xebec S1410 under GDOS 2.4:**

![GDOS 2.4 booted on the emulator: `pd 0` showing drives 5 and 6, then `dir 5` and `dir 6` listing their directories](images/gdos24-drives-5-6.png)

`src/trs_xebec.c` / `.h` implements the S1410 controller core, reached through one host-adapter interface:

- **The TCS Genie IIIs onboard SASI adapter** at ports `0x00`–`0x02`, which is what GDOS 2.4's resident hard-disk driver probes at boot. I found it by live-disassembling the driver in high RAM with scripted `zbx`. 256-byte sectors. This was the missing piece behind the long-standing "drives 5/6 never recognized" problem.

The `0x40`–`0x43` range belongs exclusively to the OMTI, never the Xebec. The adapter drives a phase-based SASI state machine (idle → CDB → data in/out → status, with real REQ/BUSY/C-D/I-O status bits and command opcodes verified against Holte's `hd2.mac`). All three controllers read and write the same Matthew Reed `.hdv` header format (`src/reed.h`).

Under real GDOS 2.4: `PD 5` and `PD 6` return drive data, `HDFORMAT` completes both passes, GDOS partitions the unit into logical drives 5 and 6, `dir` lists them, and files copied to those drives persist inside the `.hdv` across reboots.

## Hard-disk architecture

- `src/trs_hard_image.c` / `.h` — shared Reed-header `.hdv` open, geometry decode, and sector-offset math, used by all three backends.
- `src/trs_hdctl.c` / `.h` — hard-disk slots. A slot holds an image, not a controller: attach and remove reach every backend that can address that unit, and the GUI, config and write-protect code deal in slot numbers alone. The port ranges are fixed and disjoint, so each controller answers on its own whenever it has an image attached; nothing arbitrates.
- Drive caps follow each controller's real maximum: WD1000 = 4 (2-bit SDH drive field), OMTI and Xebec = 2 (1-bit SASI LUN).
- Config and CLI: hard-disk slots are controller-free — `hard0`..`hard3` (`-hard0`..`-hard3`). The legacy `-omti<n>`/`-xebec<n>` flags still map to the same slots, and `hardcontroller=` is accepted and ignored.

## Boot ROMs

On a real Genie IIIs the boot EPROM and the hard-disk controller are a matched pair — see [`docs/archaeology/original-rom-behavior.md`](docs/archaeology/original-rom-behavior.md).

- [`ROM/g3s_8501004_bootrom_2732.bin`](ROM/g3s_8501004_bootrom_2732.bin) — the standard 4 KB (2732) Genie IIIs boot ROM, and the genuine Xebec-speaking one.
- [`ROM/g3s_hd-omti_bootrom_2764.bin`](ROM/g3s_hd-omti_bootrom_2764.bin) — Arnulf Sopp's 1986 8 KB (2764) retrofit that boots from hard disk on the OMTI controller. The original OMTI port addresses (`0x40`–`0x43`) came from disassembling this ROM.

## Building

```sh
mkdir -p build && cd build && cmake .. && cmake --build .
```

or

```sh
./autogen.sh && ./configure --enable-zbx --enable-readline && make
```

## Documentation

Longer references and the investigation history are in [`docs/`](docs/README.md), grouped by what they answer:

| Directory | Answers | Highlights |
| --- | --- | --- |
| [`docs/guides/`](docs/guides/) | How do I use it? | [HOWTO.md](docs/guides/HOWTO.md) — build, attach a disk, both verified boot scenarios, the Alt-H GUI, debug flags. [changes-vs-upstream-sdltrs.md](docs/guides/changes-vs-upstream-sdltrs.md) — file-by-file divergence from stock SDL2TRS |
| [`docs/architecture/`](docs/architecture/) | How does it work today? | [controller-abstraction.md](docs/architecture/controller-abstraction.md) — three backends, one slot table, one image layer. Then one document per controller: [omti.md](docs/architecture/omti.md), [xebec.md](docs/architecture/xebec.md), [wd1000.md](docs/architecture/wd1000.md), plus [hdctl.md](docs/architecture/hdctl.md) for the slot facade |
| [`docs/archaeology/`](docs/archaeology/) | How did we find out, and why is it like this? | [design-decisions.md](docs/archaeology/design-decisions.md) — what was chosen, rejected, and why, including the decisions made twice. [controller-history.md](docs/archaeology/controller-history.md) — the drives-5/6 mystery. [recovered-source.md](docs/archaeology/recovered-source.md) — provenance of every protocol fact. [original-rom-behavior.md](docs/archaeology/original-rom-behavior.md) — what each boot EPROM can reach |
| [`docs/reference/`](docs/reference/) | What did the original documents say? | The Xebec S1410A owner's manual and SASI signal definitions, the G-DOS 2.4 material, and the 1993 German account of installing Holte's CP/M Plus on real hardware |

[`docs/README.md`](docs/README.md) is the full index.

See [`changelog.md`](changelog.md) for the dated development history.

## Related repositories

The disk images, boot EPROMs, and reverse-engineering notes this emulator is used with live in two companion archive repositories:

- **[TCS-Trommeschlaeger-Genie-IIIs](https://github.com/Egbert-Azure/TCS-Trommeschlaeger-Genie-IIIs)** — the general Genie IIIs archive: CP/M and Holte material, disks, EPROMs, source, utilities, and how-to articles.
- **[TCS-Trommeschlaeger-Genie-IIIs-GDos-2.4](https://github.com/Egbert-Azure/TCS-Trommeschlaeger-Genie-IIIs-GDos-2.4)** — the GDOS 2.4 archive: system disks, the Sopp hard-disk boot ROM, and the disassembly/investigation notes behind the drives-5/6 work.

## License

BSD 2-Clause. Portions copyright Mark Grebe and Jens Guenther; the OMTI
5527, Xebec S1410, hard-disk slot, and image-layer code is copyright
Egbert H. Schroeer. See LICENSE and AUTHORS.md.
