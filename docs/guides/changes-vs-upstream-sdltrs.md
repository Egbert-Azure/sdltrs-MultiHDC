<!-- /docs/guides/changes-vs-upstream-sdltrs.md -->

# Changes vs. upstream sdltrs

How `sdltrs-MultiHDC` diverges from stock SDL2TRS ([`gitlab.com/jengun/sdltrs`](https://gitlab.com/jengun/sdltrs), `sdl2` branch), which is the true upstream.

The baseline for comparison is the upstream commit immediately before any hard-disk-controller work (`17f2f19d`, "Refactor check of changed bits"). Two layers sit on top of it:

1. **OMTI layer**, inherited from [`sdltrsOMTI`](https://github.com/Egbert-Azure/sdltrsOMTI): OMTI 5527 SASI controller emulation and its wiring.
2. **Xebec layer**, this fork: the Xebec S1410 controller and the shared image/dispatch refactor.

Upstream's `src/` holds 47 files: 32 byte-for-byte identical, 15 modified, none removed. Of the 15, 13 appear in the Source table below; the other two are `src/Makefile` and `src/BSDmakefile`. This fork adds 8 further files to `src/`, bringing it to 55. Two more modified build files, `CMakeLists.txt` and `Makefile.am`, sit at the repo root and so fall outside the 47; `configure.ac`, also root-level, is unchanged. A third root-level build file, `meson.build` (with `meson_options.txt`), existed upstream but has been removed here — see the Build table below.

## Files added (not in upstream)

| File | Layer | What it is |
|------|-------|------------|
| `src/trs_omti.c` / `.h` | OMTI | OMTI 5527 SASI/MFM controller: phase state machine (idle → CDB → data → status), 6-byte CDBs, ports `0x40`–`0x43`. |
| `src/trs_xebec.c` / `.h` | Xebec | Xebec S1410 SASI controller on the TCS onboard adapter at ports `0x00`–`0x02` (256-byte sectors), the interface GDOS 2.4 and Holte's CP/M port (with the original EPROM) use. |
| `src/trs_hard_image.c` / `.h` | Xebec | Shared Reed-header `.hdv` open, geometry decode and sector-offset math (`hard_image_open` / `hard_image_offset`), plus the machine's slot table (`hard_slot[]`) and its save/load. Replaces three near-identical copies — and three separate sets of drives — that had lived in the WD1000, OMTI and Xebec backends. |
| `src/trs_hdctl.c` / `.h` | Xebec | Hard-disk slots. A slot holds an image, not a controller: attach/remove reach every backend that can address that unit, and the GUI, config and write-protect code deal in slot numbers alone. |

## Files modified (from upstream)

### Build

| File | Change |
|------|--------|
| `CMakeLists.txt` | Add the four new sources (`trs_omti.c`, `trs_xebec.c`, `trs_hard_image.c`, `trs_hdctl.c`). Primary build. |
| `Makefile.am` | Same four sources, for the autotools build. |
| `src/Makefile`, `src/BSDmakefile` | Same four sources, for the plain-`make` / BSD-`make` path — no configure or CMake bootstrap required, which is the reason to keep this path working: `make` (or a BSD's own `make`) is available on systems that don't have CMake. Had lagged at the OMTI-era list (`trs_omti.c` only); brought back in sync. |
| `meson.build`, `meson_options.txt` | Removed. Never documented as a supported build in the README, not exercised by anything, and redundant with CMake/autotools/make — one fewer source list to keep in sync by hand. |

Every source list has to be kept in sync by hand; there is no single source of truth. Miss one and that build system produces a binary with pieces of the hard-disk support silently absent, with no compile error. `CMakeLists.txt`, `Makefile.am`, `src/Makefile` and `src/BSDmakefile` currently agree on all 29 sources.

`configure.ac` is unchanged.

### Source

| File | +/− vs upstream | Why |
|------|-----------------|-----|
| `src/trs_io.c` | +45 / −2 | GENIE3S port dispatch for the SASI controllers (`0x40`–`0x43`, `0x00`–`0x02`) and the WD1000 remap. The port ranges are fixed and disjoint, so each controller answers on its own whenever it has an image attached; nothing arbitrates. |
| `src/trs_sdl_gui.c` | +96 / −51 | Hard Disk Management menu: one controller-free list of hard-disk slots, create-disk routing into any of them, write-protect on any slot. `MENU.type` de-`const`ed so the menu can be built at runtime. |
| `src/trs_options.c` | +33 / −2 | CLI flags `-hard0`..`-hard3` now cover every hard-disk slot whichever controller serves them; `-omti<n>`/`-xebec<n>` are legacy aliases for the same slots and `hardcontroller=` is accepted and ignored. |
| `src/trs_hard.c` | +25 / −96 | WD1000/1010 backend refactored onto the shared `trs_hard_image` helpers (net shrink), plus a LUN/drive bounds guard. |
| `src/trs_hard.h` | +1 / −1 | `TRS_HARD_MAXDRIVES` 2 → 4 (WD1000's 2-bit SDH drive field). |
| `src/trs_mkdisk.c` | +25 / −25 | `trs_write_protect` routed through `trs_hdctl` so it covers a hard-disk slot whichever controller serves it, not just WD1000. |
| `src/trs_state_save.c` | +7 / −1 | Save/load OMTI and Xebec controller state, and the slot table once via `hard_image_save`/`_load`; state version bumped to 17. |
| `src/trs_state_save.h` | +4 | Declarations for the OMTI/Xebec save/load hooks. |
| `src/trs_disk.c` | +4 | `trs_disk_init` also powers on the OMTI and Xebec controllers. |
| `src/trs.h` | +2 | Declarations for `trs_omti_debug` / `trs_xebec_debug`. |
| `src/debug.c` | +9 | `zbx` commands `omtidump`/`od` and `xebecdump`/`xd` to print controller state. |
| `src/trs_memory.c` | +1 / −1 | Comment only: note that `trs_disk_init` now also inits the hard-disk controllers. |

The machine has four hard-disk slots (`HARD_IMAGE_SLOTS`, the WD1000's cap, matching its 2-bit SDH drive field). How many of them a given controller can reach still differs: OMTI and Xebec address only units 0 and 1, the hardware limit of their 1-bit SASI LUN, and those caps live in the added `trs_omti.h` / `trs_xebec.h` rather than in any upstream file.

## Not touched

The Z80 core (`z80.c`), disassembler (`dis.c`), video/CRTC, keyboard, cassette, stringy-floppy, printer, UART, interrupt, clone-model and paste code are unchanged from upstream. The changes are confined to the hard-disk-controller path and the glue needed to wire it in (init, state-save, debug, GUI, options).