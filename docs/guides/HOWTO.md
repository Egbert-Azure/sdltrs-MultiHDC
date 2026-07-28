<!-- /docs/guides/HOWTO.md — running sdltrs-MultiHDC with a hard disk -->

# Running sdltrs-MultiHDC with a hard disk

Usage guide for both hard-disk scenarios: booting Holte's CP/M 3.0 on the OMTI controller, and running GDOS 2.4 on the Xebec. For the controller protocols, see [`../architecture/omti.md`](../architecture/omti.md) and [`../architecture/xebec.md`](../architecture/xebec.md); for how the boot ROM pairs with each controller, see [`../archaeology/original-rom-behavior.md`](../archaeology/original-rom-behavior.md).

The machine has one set of four hard-disk slots, `hard0`–`hard3`. A slot holds a disk, not a controller. All three controllers answer on their own fixed ports, and the OS on the disk decides which one it drives, so there is nothing to select — you just attach the image and boot the matching ROM.

## 1. Build

```sh
mkdir -p build && cd build && cmake .. && cmake --build .
```

The binary is `build/sdl2trs`. Rebuild after any change under `src/` (`cmake --build build` from the repo root). On macOS the `sdl2trs` window is resizable.

## 2. Attach a disk

Attach an image to a slot with `-hard0` (or `-hard1`..`-hard3`):

```sh
-hard0 HDV/g3s-omti-WORKING.hdv
```

The empty (`""`) slots in the commands below matter. `~/.sdltrs.t8c` keeps whatever was last attached to each slot, and an omitted flag does not clear a stale value. `sdl2trs` does not auto-save on quit, so anything you attached through the GUI and saved to config (Alt-menu → "Configuration/State Files") stays attached until you explicitly clear it — which is what the empty flags do.

## 3. Scenario A — Holte CP/M 3.0 on the OMTI, from hard disk

![Holte CP/M 3.0 booting from hard disk: RESBIOS3/BNKBIOS3/RESBDOS3/BNKBDOS3 loaded, 60K TPA, two Seagate ST 225 (21.4 MB) drives, C> prompt](../../images/holte-cpm3-hd-boot.png)

Boot ROM: `g3s_hd-omti_bootrom_2764.bin` (Sopp's HD-boot EPROM). Disk: `HDV/g3s-omti-WORKING.hdv`.

Use `HDV/g3s-omti-WORKING.hdv` — the only correct and complete OMTI image in `HDV/` (the rest are test images):

- Full 615-cylinder / 21.4 MB size, which the D: partition needs.
- Boots directly from the raw hard-disk EPROM with no floppy attached.
- Both C: and D: are valid, clean CP/M partitions.

One `.hdv` is one physical drive holding two logical CP/M drives, C: and D:. You attach it once at `-hard0`; there is no separate file or slot for D:. The OMTI controller has no notion of C: or D:, so to the emulated hardware that slot is a single flat block device. The split lives in the guest CP/M BIOS: `DISKIO1.MAC` (`DPBHD1`/`DPBHD2`) reads and writes the one image at two cylinder offsets, C: from cylinder 2 and D: from cylinder 307, where C: ends. The 1990s hardware worked the same way: one physical Seagate ST225 partitioned in software.

`g3s-omti-WORKING.hdv` is a live disk, not a template. Files you write to it persist. To keep a pristine copy, back it up:

```sh
cp HDV/g3s-omti-WORKING.hdv HDV/g3s-omti-WORKING.backup.hdv
```

To build a fresh one from scratch, see the docstring in `dmk-working/build_working_hdv.py` — a scripted, reproducible recipe that works around a bug in the original `COPYSYS.COM`.

Boot it, no floppy:

```sh
./build/sdl2trs -model 1 \
  -rom "/path/to/g3s_hd-omti_bootrom_2764.bin" \
  -disk0 "" -disk1 "" -disk2 "" -disk3 "" -disk4 "" -disk5 "" -disk6 "" -disk7 "" \
  -hard0 HDV/g3s-omti-WORKING.hdv -hard1 "" -hard2 "" -hard3 "" \
  -nofullscreen
```

This boots straight to a `C>` prompt: the `GENIE IIIs SYSTEM` banner, the CP/M V3.0 loader banner, all four system components, then `C>`.

To also attach a floppy (for copying files), give `-disk0` a real image instead of `""`:

```sh
./build/sdl2trs -model 1 \
  -rom "/path/to/g3s_hd-omti_bootrom_2764.bin" \
  -disk0 "dmk-working/egcpm02a.dmk" -disk1 "" ... \
  -hard0 HDV/g3s-omti-WORKING.hdv -hard1 "" -hard2 "" -hard3 "" \
  -nofullscreen
```

`dmk-working/egcpm02a.dmk` (repo root, gitignored) is a safe working copy carrying `COPY.COM` and tools. Never point `-disk0` directly at anything under your `GenieIIIs` source archive; always work from a copy.

### Copying files under CP/M

`PIP.COM` isn't on `egcpm02a.dmk`. Use `COPY.COM`, the same tool `SYSTEM.SUB` uses internally:

```
COPY A:FILENAME.EXT C:
COPY C:FILENAME.EXT A:
```

A file copied A: to C: this way is written to the `.hdv` and reads back correctly.

## 4. Scenario B — GDOS 2.4 on the Xebec

![GDOS 2.4 with the Xebec hard disk: pd 0 showing drives 5 and 6, then dir 5 and dir 6 listing their directories](../../images/gdos24-drives-5-6.png)

Boot ROM: `g3s_8501004_bootrom_2732.bin` (the standard Genie IIIs ROM). Disk: a Xebec image such as `HDV/g3s-gdos24-xebec-10mb.hdv`, attached to `-hard0`.

```sh
./build/sdl2trs -model 1 \
  -rom "/path/to/g3s_8501004_bootrom_2732.bin" \
  -disk0 "dmk-working/g3s-gdos24.dmk" -disk1 "" ... \
  -hard0 HDV/g3s-gdos24-xebec-10mb.hdv -hard1 "" -hard2 "" -hard3 "" \
  -nofullscreen
```

The standard ROM boots the floppy, and GDOS 2.4's resident driver then reaches the Xebec hard disk on its own — the disk comes up as drives 5 and 6. `PD 5` / `PD 6` report the drives, `dir 5` / `dir 6` list them, and `HDFORMAT` formats them (both partitions at once — see [`../reference/G-DOS 2-4.md`](../reference/G-DOS%202-4.md)).

## 5. GUI hard-disk management

Alt-H opens the Hard Disk Management screen: the four slots, `hard0`–`hard3`, with the attached image next to each, then the disk-set and image-creation rows.

```text
                        Hard Disk Management
 0: g3s-gdos24-xebec-10mb.hdv
*1: g3s-omti-WORKING.hdv
 2:                                             WD1000 only
 3:                                             WD1000 only

Save Disk Set
Load Disk Set

Cylinder Count                                           306
Head Count                                                 4
Sector Count                                              17
Insert Created Hard Disk Image Into Drive              None
Create Hard Disk Image with Above Parameters
```

**The asterisk means write-protected, not boot.** Nothing on this screen marks a boot slot — there is no such thing, because no controller is selected and the guest's OS decides what it boots from. Slot 1 above is protected; slot 0 is not.

Keys on a slot row:

| Key | Does |
|---|---|
| Return / Insert / Tab | Attach an image — opens the file picker at the current image's directory, or `harddir` if the slot is empty |
| Delete | Detach |
| Space | Toggle write-protect |

All three take effect immediately, without a restart. Write-protect is a property of the `.hdv` file rather than of the session: toggling it flips bit 7 of the Reed header's `flag1` byte (offset 7), chmods the file to match, and detaches and reattaches the slot. The setting therefore survives a restart, and it is honoured by whichever controller the guest drives — a protected image is opened read-only, so writes fail in all three backends.

The three geometry fields pre-fill from whichever slot is highlighted, so moving the cursor onto an attached disk shows its real cylinder/head/sector counts. Editing them and choosing **Create Hard Disk Image with Above Parameters** writes a new blank `.hdv`; set **Insert Created Hard Disk Image Into Drive** to a slot number first if you want it attached on creation. Cylinder counts above 203 are incompatible with `XTRSHARD/DCT` and log a warning.

## 6. Drive-count limits per controller

The controllers do not all reach every slot. WD1000 can address four units; the two SASI controllers reach only two, a limit of their 1-bit LUN. Slots 2 and 3 are therefore tagged `WD1000 only` in the GUI. If you attach a disk in slot 2 or 3 and the guest drives it through OMTI or Xebec, the controller rejects the out-of-range unit — a Xebec guest, for instance, gets `drive not ready`.

| Controller | Drives | Why | Guard |
|------------|--------|-----|-------|
| WD1000 | 4 | SDH drive field is 2 bits | `trs_hard.c` → `NFERR` |
| OMTI | 2 | SASI 1-bit LUN | `trs_omti.c` → command fails |
| Xebec | 2 | SASI 1-bit LUN | `trs_xebec.c` → `NOT READY` |

## 7. Expected quirks (not bugs)

- On the OMTI/Holte scenario, the boot banner prints `"Seagate ST 225 - 21.4 MB"` twice, once for C: and once for D:. The drive is 21.4 MB total, split into two ~10.4 MB partitions (C: DPB has `DSM=2591` blocks ≈ 10.4 MB; D: starts where C: ends, at cylinder 307). The init message in the original `HD2.MAC` is one hardcoded string printed on every successful drive init, and it reports the full-drive figure rather than the partition size, so it repeats. The 1990s hardware showed the same text; the partitioning is correct.
- `dir d:` shows `"No File"`. That's correct: D: is an empty second partition.

## 8. Debugging

Add `-io 0xc` to any command above for OMTI/WD1000 port and command tracing on stdout (use `-io 0x30` for Xebec DCB tracing):

```sh
./build/sdl2trs ... -io 0xc 2>&1 | grep "trs_omti: command\|ERROR"
```