/*
 * Shared Reed-format hard-disk image handling.  See trs_hard_image.h for
 * the rationale: the WD1000/1010, OMTI 5527 and Xebec S1410 backends all
 * store disks in the same Reed .hdv format and decode geometry from it
 * identically, so that common code lives here rather than in triplicate.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "error.h"
#include "reed.h"
#include "trs_hard_image.h"
#include "trs_state_save.h"

/* The machine's hard-disk slots, shared by all three controller backends.
   See trs_hard_image.h. */
HardImage hard_slot[HARD_IMAGE_SLOTS];

int hard_image_present(void)
{
  int i;

  for (i = 0; i < HARD_IMAGE_SLOTS; i++)
    if (hard_slot[i].file != NULL)
      return 1;

  return 0;
}

int hard_image_open(HardImage *d, int unit, const char *label,
                    int sec_per_trk, int maxheads)
{
  ReedHardHeader rhh;
  size_t res;
  int secs;

  if (d->filename[0] == 0)
    goto fail;

  if (d->file != NULL) {
    fclose(d->file);
    d->file = NULL;
  }

  /* First try opening for reading and writing */
  d->file = fopen(d->filename, "rb+");
  if (d->file == NULL) {
    /* No luck; try read-only (write protected) */
    if (errno == EACCES || errno == EROFS)
      d->file = fopen(d->filename, "rb");
    if (d->file == NULL) {
      file_error("open %s%d: '%s'", label, unit, d->filename);
      goto fail;
    }
    d->writeprot = 1;
  } else {
    d->writeprot = 0;
  }

  /* Read the Reed header and check some basic magic numbers (not all) */
  res = fread(&rhh, sizeof(rhh), 1, d->file);
  if (res != 1 || rhh.id1 != 0x56 || rhh.id2 != 0xcb || rhh.ver >= 0x20) {
    error("unrecognized %s%d drive image: '%s'", label, unit, d->filename);
    goto fail;
  }

  if (rhh.flag1 & 0x80) {
    /* Honor the image's write-protect flag even if the file system allows
     * read/write access. Open read-only so controller write paths fail
     * consistently on protected images. */
    if (!d->writeprot) {
      fclose(d->file);
      d->file = fopen(d->filename, "rb");
      if (d->file == NULL) {
        file_error("open %s%d readonly: '%s'", label, unit, d->filename);
        goto fail;
      }
    }
    d->writeprot = 1;
  }
 
  /* Number of cylinders from the header (0/0 means 256, per reed.h) */
  d->cyls = (rhh.cylhi << 8) | (rhh.cyllo & 0xff);

  secs = rhh.sec ? rhh.sec : 256;
  if (rhh.heads == 0) {
    /* Header gives only sectors/cylinder; assume sec_per_trk and derive
       the head count from it. */
    d->secs  = sec_per_trk;
    d->heads = secs / sec_per_trk;
  } else {
    d->heads = rhh.heads;
    d->secs  = secs / d->heads;
  }

  if ((secs % d->secs) != 0 || d->heads <= 0 || d->heads > maxheads) {
    error("unusable geometry (%d heads/%d secs) in %s%d image: '%s'",
          d->heads, d->secs, label, unit, d->filename);
    goto fail;
  }

  return 0;

fail:
  if (d->file) fclose(d->file);
  d->file = NULL;
  d->filename[0] = 0;
  return -1;
}

void hard_slot_remove(int drive)
{
  HardImage *d = &hard_slot[drive];

  if (d->file != NULL)
    fclose(d->file);

  d->filename[0] = 0;
  d->file = NULL;
  d->writeprot = 0;
  d->cyls = 0;
  d->heads = 0;
  d->secs = 0;
}

long hard_image_offset(const HardImage *d, int secsize,
                       int cyl, int head, int sec)
{
  return sizeof(ReedHardHeader) +
         (long)secsize * ((cyl * d->heads + head) * d->secs + sec);
}

void hard_image_save(FILE *file)
{
  int i;

  for (i = 0; i < HARD_IMAGE_SLOTS; i++) {
    HardImage *d = &hard_slot[i];
    int file_not_null = (d->file != NULL);

    trs_save_int(file, &file_not_null, 1);
    trs_save_filename(file, d->filename);
    trs_save_int(file, &d->writeprot, 1);
    trs_save_int(file, &d->cyls, 1);
    trs_save_int(file, &d->heads, 1);
    trs_save_int(file, &d->secs, 1);
  }
}

void hard_image_load(FILE *file)
{
  int i;

  for (i = 0; i < HARD_IMAGE_SLOTS; i++) {
    HardImage *d = &hard_slot[i];
    int file_not_null;

    if (d->file != NULL)
      fclose(d->file);

    trs_load_int(file, &file_not_null, 1);
    d->file = NULL;

    trs_load_filename(file, d->filename);
    trs_load_int(file, &d->writeprot, 1);
    trs_load_int(file, &d->cyls, 1);
    trs_load_int(file, &d->heads, 1);
    trs_load_int(file, &d->secs, 1);

    if (file_not_null == 0)
     continue;
 
    /* Reopen the image the saved state had open. Preserve a saved
     * write-protect state if the image was previously protected. */
    if (d->writeprot) {
      d->file = fopen(d->filename, "rb");
      if (d->file == NULL) {
        file_error("load hard%d: '%s'", i, d->filename);
        d->filename[0] = 0;
        d->writeprot = 0;
      }
    } else {
      d->file = fopen(d->filename, "rb+");
      if (d->file != NULL) {
        d->writeprot = 0;
      } else if ((d->file = fopen(d->filename, "rb")) != NULL) {
        d->writeprot = 1;
      } else {
        file_error("load hard%d: '%s'", i, d->filename);
        d->filename[0] = 0;
        d->writeprot = 0;
      }
    }
  }
}
