<!-- /docs/reference/G-DOS 2-4.md -->

# G-DOS 2.4

## Overview

A distinctive feature of G-DOS 2.4 is that it runs on all GENIE computers and automatically detects which model it is operating on.

In addition to the standard system files, the disk includes the following model-specific files:

| File | Purpose |
|------|---------|
| `OVL2/SYS` | Overlay for the GENIE III |
| `OVL3/SYS` | Overlay for the GENIE IIs and SpeedMaster |
| `OVL4/SYS` | Overlay for the GENIE IIIs |
| `DDE52/SYS` | DDE support for the GENIE I/II |
| `MEMDISK/CMD` | Creates a RAM disk on systems with 256 KB RAM, using Banks 2 and 3 as Drive 2 |
| `SYSCOPY/CMD` | Copies portions of DOS into Bank 1 on the GENIE IIs or IIIs for faster operation. On the GENIE IIIs it runs automatically through the BO system parameter. |

When the system disk is booted, the correct overlay for the detected computer is loaded automatically.

If no memory is present at address 3000H, `SYS15/SYS` loads `DDE52/SYS`. In that configuration DDE is not Mini-DOS compatible.

## Formatting the hard disk

The system disk includes `HDFORMAT/CMD`, a machine-language utility for formatting a hard disk.

After it starts, it asks whether the hard disk should really be formatted. To prevent accidental data loss, the answer must be `JA` ("yes") before formatting begins.

Formatting permanently erases all data on both Drive 5 and Drive 6. Selective or partial formatting is not possible.

If the hard disk is already formatted and you later want to erase the contents of a single logical drive, create a new directory for it with `GENDIR/CMD`.

Drive 6 uses a non-standard disk format and cannot be accessed with `DIRCHECK/CMD`.

## Hard disk support (GENIE IIIs only)

G-DOS 2.4 supports an internal 10 MB hard disk on the GENIE IIIs, accessed as Drive 5 and Drive 6.

A fully equipped GENIE IIIs assigns drives as follows:

| Drive | Function |
|-------|----------|
| 0–1 | Standard internal 5¼" floppy drives (80-track, DS/DD) |
| 2–3 | Optional external floppy drives |
| 4 | RAM disk (requires 256 KB RAM and `MEMDISK/CMD`) |
| 5–6 | Optional 10 MB hard disk (internal or external) |

If only the standard internal floppy drives are present, the RAM disk is assigned to Drive 2 instead.

When a program or file is requested without a drive number, G-DOS 2.4 searches in this order until it finds a match:

1. RAM disk (if installed)
2. Hard disk directory on Drive 5
3. Hard disk directory on Drive 6

---

*Translated from the G-DOS 2.4 manual. The German original is in the scanned excerpt `gdos_auszug-aus-manual.pdf`.*