/*
 * Hard-disk slots.  See trs_hdctl.h for rationale: a slot holds an image,
 * not a controller, so attaching or removing one has to reach every
 * backend that can address that unit.  The image itself lives once, in the
 * shared slot table (trs_hard_image.h); these calls only keep each
 * backend's own bookkeeping — its open file handle and decoded geometry —
 * consistent with it.
 */

#include <stddef.h>
#include "trs_hard.h"
#include "trs_hard_image.h"
#include "trs_hdctl.h"
#include "trs_mkdisk.h"
#include "trs_omti.h"
#include "trs_xebec.h"

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

void hdctl_attach(int unit, const char *filename)
{
  if (unit < TRS_HARD_MAXDRIVES)  trs_hard_attach(unit, filename);
  if (unit < TRS_OMTI_MAXDRIVES)  trs_omti_attach(unit, filename);
  if (unit < TRS_XEBEC_MAXDRIVES) trs_xebec_attach(unit, filename);
}

void hdctl_remove(int unit)
{
  if (unit < TRS_HARD_MAXDRIVES)  trs_hard_remove(unit);
  if (unit < TRS_OMTI_MAXDRIVES)  trs_omti_remove(unit);
  if (unit < TRS_XEBEC_MAXDRIVES) trs_xebec_remove(unit);
}

const char *hdctl_getfilename(int unit)
{
  return hard_slot[unit].filename;
}

int hdctl_getwriteprotect(int unit)
{
  return hard_slot[unit].writeprot;
}

void hdctl_getgeometry(int unit, int *cyls, int *heads, int *secs)
{
  if (hard_slot[unit].file) {
    *cyls  = hard_slot[unit].cyls;
    *heads = hard_slot[unit].heads;
    *secs  = hard_slot[unit].secs;
  }
}
