/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Egbert H. Schroeer
 *
 * Hard-disk slots.  See trs_hdctl.h for rationale: a slot holds an image,
 * not a controller, so attaching or removing one has to reach every
 * backend that can address that unit.  The image itself lives once, in the
 * shared slot table (trs_hard_image.h); these calls only keep each
 * backend's own bookkeeping — its open file handle and decoded geometry —
 * consistent with it.
 *
 * Original work for sdltrs-MultiHDC. Not derived from xtrs, SDLTRS,
 * or SDL2TRS. See LICENSE for the full BSD 2-Clause text.
 */

#include <stddef.h>
#include "trs_hard.h"
#include "trs_hard_image.h"
#include "trs_hdctl.h"
#include "trs_mkdisk.h"
#include "trs_omti.h"
#include "trs_xebec.h"
#include "error.h"

#if TRS_HARD_MAXDRIVES > HARD_IMAGE_SLOTS || \
    TRS_OMTI_MAXDRIVES > HARD_IMAGE_SLOTS || \
    TRS_XEBEC_MAXDRIVES > HARD_IMAGE_SLOTS
#error "a controller addresses more units than there are hard-disk slots"
#endif

/* The slot for unit, or NULL if there is no such slot. */
static HardImage *hdctl_slot(int unit)
{
  if (unit < 0 || unit >= HARD_IMAGE_SLOTS)
    return NULL;
  return &hard_slot[unit];
}

int hdctl_is_hard_type(int type)
{
  return type == HARD_DRIVE;
}

int hdctl_maxdrives(void)
{
  return HARD_IMAGE_SLOTS;
}

int hdctl_slot_wd1000_only(int unit)
{
  return unit >= TRS_OMTI_MAXDRIVES && unit >= TRS_XEBEC_MAXDRIVES;
}

/*
 * These are not "pick the controller for this unit" — that's the point.
 * Every backend whose MAXDRIVES covers unit gets the call, so all of them
 * end up attached to the same image at once. There is no notion of a
 * "selected" controller to attach to instead: nothing in this emulator
 * gates I/O on a user-chosen controller (that was bug #6), so any backend
 * wired in at all can be polled by the guest at any moment and must have
 * its own bookkeeping (file handle, geometry) already in step with the
 * slot before that happens.
 */
void hdctl_attach(int unit, const char *filename)
{
  HardImage *slot = hdctl_slot(unit);
  int served = 0;

  if (slot == NULL) {
    error("no hard-disk slot %d for '%s'", unit, filename);
    return;
  }

  if (unit < TRS_HARD_MAXDRIVES && trs_hard_attach(unit, filename) == 0)
    served = 1;

  /* An image that cannot be opened at all is closed and gone by now,
   * and no other controller will do better with it.  One that a
   * controller merely refuses (too many heads) stays open for the rest. */
  if (slot->file != NULL) {
    if (unit < TRS_OMTI_MAXDRIVES && trs_omti_attach(unit, filename) == 0)
      served = 1;
    if (unit < TRS_XEBEC_MAXDRIVES && trs_xebec_attach(unit, filename) == 0)
      served = 1;
  }

  /* Nobody can address it: don't leave it in the slot. */
  if (!served)
    hard_slot_remove(unit);
}

void hdctl_remove(int unit)
{
  /* trs_hard_remove() runs the xtrshard import/export hook and clears the
   * shared slot, which is all removal needs for OMTI and Xebec too.
   * Neither step depends on the WD1000's unit count, so every slot goes
   * through it. */
  if (hdctl_slot(unit) != NULL)
    trs_hard_remove(unit);
}

const char *hdctl_getfilename(int unit)
{
  const HardImage *slot = hdctl_slot(unit);

  return slot ? slot->filename : "";
}

int hdctl_getwriteprotect(int unit)
{
  const HardImage *slot = hdctl_slot(unit);

  return slot ? slot->writeprot : 0;
}

void hdctl_getgeometry(int unit, int *cyls, int *heads, int *secs)
{
  const HardImage *slot = hdctl_slot(unit);

  if (slot && slot->file) {
    *cyls  = slot->cyls;
    *heads = slot->heads;
    *secs  = slot->secs;
  }
}
