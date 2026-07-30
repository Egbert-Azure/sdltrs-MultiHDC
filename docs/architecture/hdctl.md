<!-- /docs/architecture/hdctl.md -->

# `trs_hdctl` — the hard-disk slot facade

`src/trs_hdctl.c` / `.h` is a small file with one job: let the GUI, the config loader and the command line deal in **slot numbers alone**, and keep all three controller backends in step behind that. It is about 60 lines and should stay that way.

For why it exists at all, and how it relates to the image layer underneath, read `controller-abstraction.md` first.

## The rule it enforces

> A slot holds a disk image, not a controller.

The user says "put this `.hdv` in slot 0". They do not also say which controller reads it, because a real Genie IIIs has exactly one fitted and the OS on the disk decides which one it addresses. So `hdctl_attach(0, file)` tells *every* backend that can reach unit 0 about it, and whichever one the guest drives is the one that serves it.

## API

| Call | What it does |
|---|---|
| `hdctl_is_hard_type(type)` | True if a GUI menu-row type is the hard-disk one, as opposed to floppy, wafer or cassette |
| `hdctl_maxdrives()` | Number of slots the machine has — `HARD_IMAGE_SLOTS`, i.e. 4 |
| `hdctl_slot_wd1000_only(unit)` | True if only the WD1000 can reach this slot |
| `hdctl_attach(unit, filename)` | Attach an image to a slot |
| `hdctl_remove(unit)` | Detach |
| `hdctl_getfilename(unit)` | Current image path, for display |
| `hdctl_getwriteprotect(unit)` | Write-protect flag, for display and toggling |
| `hdctl_getgeometry(unit, &cyls, &heads, &secs)` | Decoded geometry, for display |

## Two kinds of call, and why they differ

The four **getters** read `hard_slot[]` directly:

```c
const char *hdctl_getfilename(int unit)
{
  return hard_slot[unit].filename;
}
```

There is nothing to ask a backend. The image lives once, in the shared slot table, and its filename, write-protect flag and geometry are properties of the image — the same answer whichever controller you ask. OMTI and Xebec never had their own `_getfilename()`/`_getwriteprotect()`/`_getgeometry()` equivalents worth keeping and don't now; only the WD1000's `trs_hard_getfilename()` is still used directly, by `trs_disk.c` and `trs_imp_exp.c`. New code should call the `hdctl_` form.

**Attach** is a real fan-out — every backend that can reach the unit needs told, because each one caches its own open `FILE *` and decoded geometry, and skipping one would leave it holding a stale handle to a disk that is no longer there:

```c
void hdctl_attach(int unit, const char *filename)
{
  if (unit < TRS_HARD_MAXDRIVES)  trs_hard_attach(unit, filename);
  if (unit < TRS_OMTI_MAXDRIVES)  trs_omti_attach(unit, filename);
  if (unit < TRS_XEBEC_MAXDRIVES) trs_xebec_attach(unit, filename);
}
```

The image itself is opened once by the image layer; what each backend needs told is *its own bookkeeping*.

The `unit <` guards are the LUN limit, written as a comparison against each backend's own published cap rather than a hardcoded 2. `TRS_HARD_MAXDRIVES` is 4 (WD1000's two-bit SDH drive field); `TRS_OMTI_MAXDRIVES` and `TRS_XEBEC_MAXDRIVES` are 2 (1-bit SASI LUN). Change a cap in one of the controller headers and this file follows automatically.

**Remove is not a fan-out**, unlike attach — removal has no per-backend geometry/handle to update, only the shared slot to clear, plus one real side effect on the WD1000 side (the xtrshard import/export hook):

```c
void hdctl_remove(int unit)
{
  if (unit < TRS_HARD_MAXDRIVES) {
    trs_hard_remove(unit);
    hard_slot_remove(unit);
  }
}
```

OMTI and Xebec used to each have their own `trs_omti_remove()`/`trs_xebec_remove()`, but both were pure `{ hard_slot_remove(drive); }` pass-throughs with no backend-specific bookkeeping — removed as dead wrappers (#19). `TRS_HARD_MAXDRIVES` alone bounds every valid unit here since it equals `HARD_IMAGE_SLOTS`, the largest of the three caps (see `controller-abstraction.md`).

`hdctl_slot_wd1000_only()` is the same idea inverted:

```c
return unit >= TRS_OMTI_MAXDRIVES && unit >= TRS_XEBEC_MAXDRIVES;
```

The GUI uses it to tag slots 2 and 3. It is advisory labelling only — nothing stops you attaching a disk there. If you do, and the guest drives it through OMTI or Xebec, the controller rejects the out-of-range unit as not present (a Xebec guest gets `NOT READY`). That is what the real hardware does too.

## What it deliberately does not do

- **No port dispatch.** `trs_io.c` routes I/O by port range; `trs_hdctl.c` never sees a port.
- **No notion of an active controller.** There isn't one. See `../archaeology/design-decisions.md` on issue #6.
- **No file I/O.** Opening, header parsing and offset math all live in `trs_hard_image.c`.
- **No write-protect enforcement.** The flag is read back here for display and toggling only. It is enforced one layer down, in `hard_image_open()`, by opening a protected image read-only so that every backend's write path fails whether or not it remembered to check the flag — which the OMTI's never did. See `../archaeology/design-decisions.md`.

## Callers

| File | Uses it for |
|---|---|
| `src/trs_sdl_gui.c` | The Alt-H Hard Disk Management screen — slot list, geometry display, write-protect column, the `WD1000 only` tag, and the insert/create popups |
| `src/trs_options.c` | `-hard0`..`-hard3`, the `.t8c` config read and write, and clearing all slots on reset |
| `src/trs_mkdisk.c` | Creating a blank image into a slot, and the write-protect toggle (which detaches and reattaches) |

State saving does not go through here: the slots are serialised by the image layer's `hard_image_save()` / `hard_image_load()`.
