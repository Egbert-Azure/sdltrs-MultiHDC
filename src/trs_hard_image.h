/*
 * Shared Reed-format hard-disk image handling.
 *
 * The three hard-disk controller backends in this emulator — the Western
 * Digital WD1000/1010 (trs_hard.c), the OMTI 5527 (trs_omti.c) and the
 * Xebec S1410 (trs_xebec.c) — implement genuinely different controller
 * protocols, but they all store their disks in Matthew Reed's 256-byte
 * header .hdv format (reed.h) and decode geometry from it identically.
 * That common part lives here so the three backends share one copy
 * instead of three near-identical ones.
 *
 * This module is deliberately protocol-agnostic: it only knows about the
 * image file and its geometry, never about controller registers, phases,
 * or status bytes.  Each backend keeps its own controller state and wraps
 * these helpers with whatever protocol-specific side effects it needs.
 */

#ifndef _TRS_HARD_IMAGE_H
#define _TRS_HARD_IMAGE_H

#include <stdio.h>

/*
 * One attached image and the geometry decoded from its Reed header.
 * This is the "Drive" record each backend keeps an array of.
 */
typedef struct {
  FILE *file;
  char  filename[FILENAME_MAX];
  int   writeprot;
  int   cyls;   /* cylinders per drive        */
  int   heads;  /* heads (tracks) per cylinder */
  int   secs;   /* sectors per track          */
} HardImage;

/*
 * The machine's hard-disk slots.
 *
 * A slot holds a disk image, not a controller.  A real Genie IIIs has one
 * hard-disk controller fitted and the operating system on the disk decides
 * which one it addresses, so the user has no reason to say it twice: the
 * three backends all read and write these same slots, and whichever
 * controller the guest drives is the one that serves them.  The count is
 * the WD1000's, the largest of the three; the SASI controllers reach only
 * units 0 and 1, which their own 1-bit LUN already enforces.
 */
#define HARD_IMAGE_SLOTS 4

extern HardImage hard_slot[HARD_IMAGE_SLOTS];

/*
 * Is a hard disk fitted?  True once any slot has an image open.  The three
 * backends share this rather than each latching its own flag at power-on,
 * so a disk attached from the GUI takes effect immediately instead of
 * waiting for a reset, and an empty machine reads back 0xFF on every
 * controller's ports.
 */
extern int hard_image_present(void);

/*
 * (Re)open d->filename, parse its Reed header, and fill in d->writeprot
 * and the geometry fields.
 *
 *   unit        - drive index, used only in diagnostic messages.
 *   label       - controller name for diagnostics ("hard"/"omti"/"xebec").
 *   sec_per_trk - assumed sectors/track when the header omits a head count.
 *   maxheads    - upper bound accepted for the decoded head count.
 *
 * On success returns 0 with d fully populated (d->file open).  On failure
 * closes the file, clears d->filename, and returns -1; the caller is
 * responsible for any controller-specific error state (e.g. WD1000's
 * error register).
 */
extern int hard_image_open(HardImage *d, int unit, const char *label,
                           int sec_per_trk, int maxheads);

/*
 * Close (if open) and clear the slot: filename, file handle, write-protect
 * and geometry all reset to empty.  The caller is responsible for any
 * controller-specific side effect of removal (e.g. WD1000's xtrshard
 * import/export hook).
 */
extern void hard_slot_remove(int drive);

/*
 * Byte offset of a sector within the image, past the Reed header.  cyl,
 * head and sec must already be reduced to the drive's geometry (sec in
 * 0..secs-1).  secsize is the controller's current bytes-per-sector.
 */
extern long hard_image_offset(const HardImage *d, int secsize,
                              int cyl, int head, int sec);

/*
 * Save / restore the slot table.  The slots belong to the machine, not to
 * any one controller, so they are serialised here once rather than by each
 * backend in turn.
 */
extern void hard_image_save(FILE *file);
extern void hard_image_load(FILE *file);

#endif /* _TRS_HARD_IMAGE_H */
