<!-- /docs/archaeology/original-rom-behavior.md -->

# Boot EPROM and hard-disk controller pairing (Genie IIIs)

On a real Genie IIIs the boot EPROM and the hard-disk controller are a matched pair. In the emulator they are two independent settings — the `romfile` you load, and the disk you attach — and nothing cross-checks them, so you can pick a combination that cannot work.

## The two boot EPROMs

Both dumps are in `ROM/`:

| File | Size | EPROM | Role |
|------|------|-------|------|
| `g3s_8501004_bootrom_2732.bin` | 4 KB | 2732 | Standard Genie IIIs boot ROM |
| `g3s_hd-omti_bootrom_2764.bin` | 8 KB | 2764 | OMTI HD-boot ROM (Arnulf Sopp's modification) |

The OMTI ROM is twice the size. Sopp doubled the EPROM to fit the extra hard-disk-boot code, so HD-boot support was bolted onto the standard ROM rather than being part of it. That size difference is a usable discriminator (below).

## Confirmed

The standard EPROM talks only to the Xebec/TCS side, never to OMTI. It drives the TCS onboard SASI adapter at ports `0x00`–`0x02`, the same interface GDOS 2.4's resident driver probes. OMTI sits on a different adapter (`0x40`–`0x42`, Holte's slot) and the stock ROM has no code for it. OMTI is therefore unreachable without the OMTI ROM.

## Boot order, by EPROM

- OMTI ROM: hard disk first, then floppy.
- Standard ROM: floppy first, and effectively floppy-only for the boot step. GDOS reaches the Xebec hard disk only afterwards, through its resident driver.

Both follow from what each ROM is for: an HD-boot EPROM tries the disk and falls back to floppy, and the stock ROM doesn't boot from hard disk at all.

## The pairing is not 1:1

The boot ROM doesn't uniquely determine the controller. The standard ROM serves two of them, and which one is decided by the OS on the floppy, which the emulator can't see:

| Boot ROM | OS | Controller | Drive cap |
|----------|------|-----------|-----------|
| `8501004` (4 KB / 2732) | GDOS 2.4 | Xebec | 2 |
| `8501004` (4 KB / 2732) | CP/M | WD1000 | 4 |
| `hd-omti` (8 KB / 2764) | — | OMTI | 2 |

So `8501004` means Xebec or WD1000, and only the OMTI ROM is unambiguous. Full auto-select isn't possible, but a validity check would be, keyed on ROM size:

- 8 KB (2764) means OMTI-boot, so the controller must be OMTI.
- 4 KB (2732) means standard, so the controller must be Xebec or WD1000; OMTI is invalid. The OS picks which, so the user does too.

The drive caps (Xebec 2, OMTI 2, WD1000 4) already match this table in the current build.

## Proposed emulator behaviour (not implemented, superseded by #6)

Kept as a design record. #6 settled the question the other way: the emulator does not enforce which controller is fitted. A real Genie IIIs has exactly one, but the OS on the disk already decides which one it addresses, and no Genie IIIs software looks across the port ranges to notice a second one. The pairings above stay true as guidance — a mismatched EPROM and slot still means the disk isn't found — they are just not policed in code.

The idea was a cross-check keyed on boot-ROM size that would reject the impossible combinations (OMTI ROM with a non-OMTI slot, or standard ROM with the OMTI slot) and set a sensible default: OMTI ROM to OMTI, standard ROM to Xebec as the GDOS-native case, while still allowing a switch to WD1000 for CP/M.

Two things would have needed deciding first: how strict to be (default-and-warn with overrides allowed, constrain the controller menu, or warn only), and how to identify the ROM (by size, by content hash of the two known dumps, or size with hash as a tiebreak).

## In practice

Only the boot ROM is yours to pick; the disk brings its own OS, and that OS drives whichever controller it was built for. Attach the `.hdv` to a hard-disk slot (`-hard0`) either way:

- **GDOS 2.4 / Xebec:** `g3s_8501004_bootrom_2732.bin`
- **OMTI boot:** `g3s_hd-omti_bootrom_2764.bin`
- **CP/M / WD1000:** `g3s_8501004_bootrom_2732.bin`

If the ROM and the disk's OS disagree, the disk simply isn't found. The two SASI controllers reach only slots 0 and 1, a limit of their 1-bit LUN; the WD1000 reaches all four.