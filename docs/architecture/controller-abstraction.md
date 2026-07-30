<!-- /docs/architecture/controller-abstraction.md -->

# Three controllers, one set of disks

How the three hard-disk controller backends coexist in one emulated Genie IIIs. Read this before `omti.md`, `xebec.md` or `wd1000.md` — each of those describes one protocol, and this describes what they share.

## The problem

A TCS Genie IIIs can be fitted with any of three hard-disk controllers, and this fork emulates all three:

| Backend | Chip | Ports (GENIE3S) | Style | Units |
|---|---|---|---|---|
| `trs_hard.c` | WD1000/1010 | `0x50`–`0x57` | Register-based | 4 |
| `trs_omti.c` | OMTI 5527 | `0x40`–`0x43` | Phase-based SASI | 2 |
| `trs_xebec.c` | Xebec S1410 | `0x00`–`0x02` | Phase-based SASI | 2 |

They are genuinely different protocols — see the per-controller docs — but they are three ways of reaching *the same disks*. Each one grew its own copy of the `.hdv` open/geometry/offset code and its own `Drive drives[]` array, which meant three separate sets of drives in one machine and three near-identical copies of the same file math. The abstraction exists to collapse that.

## Two layers, and what each one is not

```text
  GUI (Alt-H) · config (.t8c) · command line (-hard0..3)
                          │
                          ▼
              trs_hdctl.c  — slot facade
              "attach unit 2" → tell every backend that can reach unit 2
                          │
        ┌─────────────────┼─────────────────┐
        ▼                 ▼                 ▼
   trs_hard.c        trs_omti.c        trs_xebec.c
   WD1000 regs       SASI phases       SASI phases
        └─────────────────┼─────────────────┘
                          ▼
            trs_hard_image.c  — the image layer
            hard_slot[4], Reed header, geometry, offset math
```

**`trs_hard_image.c` is deliberately protocol-agnostic.** It knows about the image file and its geometry and nothing else — no controller registers, no phases, no status bytes. It owns `hard_slot[HARD_IMAGE_SLOTS]`, the machine's four slots, and provides:

- `hard_image_present()` — true once any slot has an image open. The three backends share this instead of each latching its own flag at power-on, so a disk attached from the GUI takes effect immediately rather than at the next reset, and an empty machine floats `0xFF` on every controller's ports.
- `hard_image_open(d, unit, label, sec_per_trk, maxheads)` — (re)open, parse the Reed header, fill in `writeprot` and the geometry. The `label` is only for diagnostics (`"hard"`/`"omti"`/`"xebec"`); `sec_per_trk` and `maxheads` are the per-controller assumptions the header decode needs. On failure it closes the file and clears the filename, leaving any controller-specific error state (WD1000's error register, Xebec's sense code) to the caller.
- `hard_image_offset(d, secsize, cyl, head, sec)` — byte offset past the 256-byte header. `secsize` is passed in because it is the *controller's* current bytes-per-sector, not a property of the image: WD1000 has a live sector-size register, while OMTI and Xebec fix theirs with a jumper (`trs_omti_secsize`/`trs_xebec_secsize`, settable via `-omtisecsize`/`-xebecsecsize`, defaulting to 512/256) read once at reset.
- `hard_image_save()` / `hard_image_load()` — the slots belong to the machine, not to any one controller, so they are serialised once here rather than three times.

**`trs_hdctl.c` is not a dispatcher.** It never routes an I/O port and never decides which controller is "active" — that is `trs_io.c`'s job, and it does it by port range alone. `trs_hdctl.c` only fans slot-level operations out to whichever backends can address that unit. See `hdctl.md`.

## No arbitration, by design

The three port ranges are fixed and disjoint, so `trs_io.c`'s `GENIE3S` dispatch routes each range to its own backend and nothing arbitrates between them:

```c
case 0x50 ... 0x57:  trs_hard_out(port + 0x78, value);  /* → 0xC8-0xCF */
case 0x40 ... 0x43:  trs_omti_out(port, value);
case 0x00 ... 0x02:  trs_xebec_tcs_out(port, value);
```

Each backend answers on its own ports whenever a disk is fitted and is inert otherwise. A real Genie IIIs has exactly one controller card installed, but the operating system on the disk already decides which one it addresses, and no Genie IIIs software looks across the port ranges to notice a second one — so the emulator does not enforce a single fitted controller. An earlier build did gate I/O on a user-selected controller; that was bug #6. See `../archaeology/design-decisions.md`.

The WD1000's `0x50`–`0x57` is a relocation: the Tandy original lives at `0xC8`–`0xCF`, and the `+ 0x78` shift in `trs_io.c` maps the Genie IIIs range onto the unmodified upstream backend rather than teaching `trs_hard.c` a second port map.

## Slot count and the LUN limit

`HARD_IMAGE_SLOTS` is 4 — the WD1000's cap, the largest of the three, because its SDH drive field is two bits wide. The two SASI controllers have a 1-bit LUN and so reach only units 0 and 1.

Nothing in the image layer knows that. The limit is enforced where it actually is on the hardware: in each backend's own LUN decode, which fails an out-of-range unit as *not present* rather than indexing `hard_slot[]` out of bounds. `hdctl_slot_wd1000_only()` exists purely so the GUI can label slots 2 and 3, and it is written as a question about the backends' caps rather than a hardcoded `unit >= 2`.

## Shared image format

All three read and write Matthew Reed's `.hdv` format (`src/reed.h`, `ReedHardHeader`): a 256-byte header (magic `0x56 0xCB`, geometry, write-protect flag) followed by raw sector data, with no interleave or skew reordering. The offset is a flat linear shift:

```text
file_offset = 256 + ((cyl * heads + head) * secs + sector) * secsize
```

`trs_mkdisk.c` creates blank images in this format. Geometry always comes from the image's header, never from what a guest driver declares — every backend ignores its own "set drive characteristics" command for addressing purposes, for reasons spelled out in `omti.md`.

## Source map

| File | Role |
|---|---|
| `src/trs_hard_image.c` / `.h` | Slot table, Reed header decode, offset math, save/load |
| `src/trs_hdctl.c` / `.h` | Slot facade for GUI, config and command line |
| `src/trs_io.c` (`GENIE3S` blocks) | Port-range dispatch to the three backends |
| `src/trs_hard.c`, `trs_omti.c`, `trs_xebec.c` | The three protocols |
