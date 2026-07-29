/*
 * Copyright (c) 1996-2020, Timothy P. Mann
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Emulation of the Radio Shack TRS-80 Model I/III/4/4P
 * hard disk controller.  This is a Western Digital WD1000/WD1010
 * mapped at ports 0xc8-0xcf, plus control registers at 0xc0-0xc1.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "error.h"
#include "reed.h"
#include "trs.h"
#include "trs_hard.h"
#include "trs_hard_image.h"
#include "trs_imp_exp.h"
#include "trs_state_save.h"

#define HARDDEBUG1 (1 << 2)  /* show detail on all port i/o */
#define HARDDEBUG2 (1 << 3)  /* show all commands */
#define SECTORSIZE (1024)

/* Private types and data */

/* One attached drive image; geometry decoding is shared (trs_hard_image.h) */

/* Structure describing controller state */
typedef struct {
  /* Controller register images */
  Uint8  control;
  Uint8  error;
  Uint8  seccnt;
  Uint8  secnum;
  Uint16 secsize;
  Uint16 cyl;
  Uint8  drive;
  Uint8  head;
  Uint8  sdh;
  Uint8  status;
  Uint8  command;
  Uint8  secbuf[SECTORSIZE];

  /* Number of bytes already done in current read/write */
  int bytesdone;
} State;

static State state;

/* Forward */
static void hard_error(int error);
static int  hard_data_in(void);
static void hard_data_out(int value);
static void hard_restore(void);
static void hard_read(int cmd);
static void hard_write(int cmd);
static void hard_verify(void);
static void hard_format(void);
static void hard_init(int cmd);
static void hard_seek(void);
static int  hard_open(int drive);
static int  hard_sector(int newstatus);

#ifdef ZBX
static const char *hard_cmd(int port)
{
  switch (port) {
    case TRS_HARD_WP:
      return "c0:WP";
    case TRS_HARD_CONTROL:
      return "c1:CONTROL";
    case TRS_HARD_DATA:
      return "c8:DATA";
    case TRS_HARD_PRECOMP:
      return "c9:PRECOMP/ERROR";
    case TRS_HARD_SECCNT:
      return "ca:SECCNT";
    case TRS_HARD_SECNUM:
      return "cb:SECNUM";
    case TRS_HARD_CYLLO:
      return "cc:CYLLO";
    case TRS_HARD_CYLHI:
      return "cd:CYLHI";
    case TRS_HARD_SDH:
      return "ce:SDH";
    case TRS_HARD_COMMAND:
      return "cf:COMMAND/STATUS";
    default:
      return "";
  }
}

static void hard_debug(const char *cmd)
{
  debug("hard_%s: drive:%d, head:%d, cyl:%4d, secnum:%3d, seccnt:%3d\n",
    cmd, state.drive, state.head, state.cyl, state.secnum, state.seccnt);
}

static void sector_buf(const char *cmd)
{
  int byte[16];
  int i;

  for (i = 0; i < state.secsize; i += 16) {
    int j;

    debug("hard_%s: %04x: ", cmd, i);
    for (j = 0; j < 16; j++)
      fprintf(stderr, "%02x ", (byte[j] = state.secbuf[i + j]));

    fprintf(stderr, "   ");
    for (j = 0; j < 16; j++)
      putc(TOPRINT(byte[j]), stderr);

    putc('\n', stderr);
  }
}

void trs_hard_debug(void)
{
  int i;

  printf("Hard disk controller state:");
  if (hard_image_present() == 0) {
    puts(" DISABLED");
    return;
  }

  printf("\n  hard drive:%d, head:%d, cyl:%d, secnum:%d, seccnt:%d, secsize:%d\n",
      state.drive, state.head, state.cyl, state.secnum, state.seccnt, state.secsize);
  printf("  status:0x%02X, command:0x%02X, control:0x%02X, error:0x%02X\n",
      state.status, state.command, state.control, state.error);

  for (i = 0; i < TRS_HARD_MAXDRIVES; i++) {
    if (hard_slot[i].file) {
      printf("\nhard%d: '%s'\n", i, hard_slot[i].filename);
      printf("\theads %d, cyls %4d, secs %4d, writeprot %d\n",
          hard_slot[i].heads, hard_slot[i].cyls, hard_slot[i].secs, hard_slot[i].writeprot);
    }
  }
}
#endif

/* Powerup or reset button */
void trs_hard_init(int poweron)
{
  state.control = 0;
  state.error = 0;
  state.secnum = 0;
  state.secsize = 0;
  state.cyl = 0;
  state.drive = 0;
  state.head = 0;
  state.sdh = 0;
  state.command = 0;
  memset(state.secbuf, 0, SECTORSIZE);

  if (poweron) {
    int i;

    state.seccnt = 0;
    state.status = 0;

    for (i = 0; i < TRS_HARD_MAXDRIVES; i++) {
      hard_slot[i].writeprot = 0;
      hard_slot[i].cyls = 0;
      hard_slot[i].heads = 0;
      hard_slot[i].secs = 0;

      hard_open(i);
    }
  }
}

void trs_hard_attach(int drive, const char *diskname)
{
  snprintf(hard_slot[drive].filename, FILENAME_MAX, "%s", diskname);

  if (hard_open(drive) == 0)
    trs_impexp_xtrshard_attach(drive);
  else
    trs_hard_remove(drive);

}

void trs_hard_remove(int drive)
{
  trs_impexp_xtrshard_remove(drive);
  hard_slot_remove(drive);
}

const char*
trs_hard_getfilename(int unit)
{
  return hard_slot[unit].filename;
}

int
trs_hard_getwriteprotect(int unit)
{
  return hard_slot[unit].writeprot;
}

void
trs_hard_getgeometry(int unit, int *cyls, int *head, int *secs)
{
  if (hard_slot[unit].file) {
    *cyls = hard_slot[unit].cyls;
    *head = hard_slot[unit].heads;
    *secs = hard_slot[unit].secs;
  }
}

/* Read from an I/O port mapped to the controller */
int trs_hard_in(int port)
{
  int v = 0xff;

  if (hard_image_present()) {
    switch (port) {
    case TRS_HARD_WP: {
      int i;

      v = 0;
      for (i = 0; i < TRS_HARD_MAXDRIVES; i++) {
        if (hard_open(i) == 0) {
          if (hard_slot[i].writeprot) {
            v |= TRS_HARD_WPBIT(i) | TRS_HARD_WPSOME;
          }
        }
      }
      break; }
    case TRS_HARD_CONTROL:
      v = state.control;
      break;
    case TRS_HARD_DATA:
      v = hard_data_in();
      break;
    case TRS_HARD_ERROR:
      v = state.error;
      break;
    case TRS_HARD_SECCNT:
      v = state.seccnt;
      break;
    case TRS_HARD_SECNUM:
      v = state.secnum;
      break;
    case TRS_HARD_CYLLO:
      v = state.cyl & 0xff;
      break;
    case TRS_HARD_CYLHI:
      v = (state.cyl >> 8) & 0xff;
      break;
    case TRS_HARD_SDH:
      v = state.sdh;
      break;
    case TRS_HARD_STATUS:
      v = state.status;
      break;
    }
  }
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG1)
    debug("[PC=%04X] trs_hard_in(%s) => %02X\n", Z80_PC, hard_cmd(port), v);
#endif
  return v;
}

/* Write to an I/O port mapped to the controller */
void trs_hard_out(int port, int value)
{
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG1)
    debug("[PC=%04X] trs_hard_out(%s), %02X\n", Z80_PC, hard_cmd(port), value);
#endif
  /* No image attached: no card in the machine, so ignore writes -- the
   * matching reads in trs_hard_in() already float high. */
  if (hard_image_present() == 0)
    return;

  switch (port) {
  case TRS_HARD_WP:
    break;
  case TRS_HARD_CONTROL:
    if (value & TRS_HARD_SOFTWARE_RESET)
      trs_hard_init(0);

    if (value & TRS_HARD_DEVICE_ENABLE)
      trs_hard_init(1);

    state.control = value;
    break;
  case TRS_HARD_DATA:
    hard_data_out(value);
    break;
  case TRS_HARD_PRECOMP:
    break;
  case TRS_HARD_SECCNT:
    state.seccnt = value;
    break;
  case TRS_HARD_SECNUM:
    state.secnum = value;
    break;
  case TRS_HARD_CYLLO:
    state.cyl = (state.cyl & 0xff00) | (value & 0x00ff);
    break;
  case TRS_HARD_CYLHI:
    state.cyl = (state.cyl & 0x00ff) | ((value << 8) & 0xff00);
    break;
  case TRS_HARD_SDH:
    state.sdh = value;
    state.secsize = 256 << ((value & TRS_HARD_SIZEMASK) >> TRS_HARD_SIZESHIFT);
    if (state.secsize > SECTORSIZE) state.secsize = 128;
    state.drive = (value & TRS_HARD_DRIVEMASK) >> TRS_HARD_DRIVESHIFT;
    state.head = (value & TRS_HARD_HEADMASK) >> TRS_HARD_HEADSHIFT;
#if 0
    if (hard_open(state.drive) == 0) state.status &= ~TRS_HARD_READY;
#else
    /* Ready, but perhaps not able!  This way seems to work better; it
     * avoids a long delay in the Model 4P boot ROM when there is no
     * unit 0. */
    state.status = TRS_HARD_READY | TRS_HARD_SEEKDONE;
#endif
    break;

  case TRS_HARD_COMMAND:
    state.bytesdone = 0;
    state.command = value;
    /* SDH's drive field is 2 bits (0-3), so guest software can select a
     * unit past TRS_HARD_MAXDRIVES even though only that many are
     * emulated: treat it as not-present rather than indexing hard_slot[]
     * out of bounds. */
    if (state.drive >= TRS_HARD_MAXDRIVES) {
      hard_error(TRS_HARD_NFERR);
      break;
    }
    switch (value & TRS_HARD_CMDMASK) {
    case TRS_HARD_RESTORE:
      hard_restore();
      break;
    case TRS_HARD_READ:
      hard_read(value);
      break;
    case TRS_HARD_WRITE:
      hard_write(value);
      break;
    case TRS_HARD_VERIFY:
      hard_verify();
      break;
    case TRS_HARD_FORMAT:
      hard_format();
      break;
    case TRS_HARD_INIT:
      hard_init(value);
      break;
    case TRS_HARD_SEEK:
      hard_seek();
      break;
    default:
      error("trs_hard: unknown command 0x%02X", value);
      break;
    }
    break;

  }
}

static void hard_error(int error)
{
  state.status = TRS_HARD_READY | TRS_HARD_SEEKDONE | TRS_HARD_ERR;
  state.error  = error;
}

static void hard_restore(void)
{
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG2)
    debug("hard_restore drive:%d\n", state.drive);
#endif
  state.cyl = 0;
  /*!! should anything else be zeroed? */
  state.status = TRS_HARD_READY | TRS_HARD_SEEKDONE;
}

static void hard_read(int cmd)
{
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG2)
    hard_debug("read");
#endif
  if (cmd & TRS_HARD_MULTI) {
    error("trs_hard: multi-sector read not supported (0x%02X)", cmd);
    hard_error(TRS_HARD_ABRTERR);
    return;
  }
  if (hard_sector(TRS_HARD_READY | TRS_HARD_SEEKDONE | TRS_HARD_DRQ) == 0) {
    FILE *f = hard_slot[state.drive].file;

    if (f && fread(state.secbuf, 1, state.secsize, f) != state.secsize) {
      if (ferror(f)) {
        file_error("reading hard%d", state.drive);
        hard_error(TRS_HARD_DATAERR); /* arbitrary choice */
      }
    }
#if ZBX
    if (trs_io_debug_flags & HARDDEBUG2)
      sector_buf("read");
#endif
  }
}

static void hard_write(int cmd)
{
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG2)
    hard_debug("write");
#endif
  if (cmd & TRS_HARD_MULTI) {
    error("trs_hard: multi-sector write not supported (0x%02X)", cmd);
    hard_error(TRS_HARD_ABRTERR);
    return;
  }
  hard_sector(TRS_HARD_READY | TRS_HARD_SEEKDONE | TRS_HARD_DRQ);
}

static void hard_verify(void)
{
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG2)
    hard_debug("verify");
#endif
  hard_sector(TRS_HARD_READY | TRS_HARD_SEEKDONE);
}

static void hard_format(void)
{
  FILE *f = hard_slot[state.drive].file;

#if ZBX
  if (trs_io_debug_flags & HARDDEBUG2)
    hard_debug("format");
#endif

  if (f) {
    int cnt = state.seccnt;

    memset(state.secbuf, 0, SECTORSIZE);
    while (cnt--) {
      if (fwrite(state.secbuf, 1, state.secsize, f) != state.secsize) {
        if (errno) {
          file_error("formatting hard%d", state.drive);
          hard_error(TRS_HARD_DATAERR); /* arbitrary choice */
          return;
        }
      }
    }
  }
  /* !!should probably set up to read skew table here */
  state.seccnt = 0;
  state.status = TRS_HARD_READY | TRS_HARD_SEEKDONE;
}

static void hard_init(int cmd)
{
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG2)
    hard_debug("init");
#endif
  /* I don't know what this command does */
  error("trs_hard: init command (0x%02X) not implemented", cmd);
  state.status = TRS_HARD_READY | TRS_HARD_SEEKDONE;
}

static void hard_seek(void)
{
#if ZBX
  if (trs_io_debug_flags & HARDDEBUG2)
    hard_debug("seek");
#endif
  hard_sector(TRS_HARD_READY | TRS_HARD_SEEKDONE);
}

/*
 * Make sure the file for the given drive is open, decoding its Reed
 * header and geometry (via the shared trs_hard_image helper).  On success
 * set the controller ready/seek-done status and return 0; on failure set
 * the controller error status and return -1.
 */
static int hard_open(int drive)
{
  HardImage *d = &hard_slot[drive];

  if (hard_image_open(d, drive, "hard",
                      TRS_HARD_SEC_PER_TRK, TRS_HARD_MAXHEADS) != 0) {
    hard_error(TRS_HARD_NFERR);
    return -1;
  }

#if ZBX
  if (trs_io_debug_flags & HARDDEBUG2)
    debug("hard_open: drive:%d, cyls:%d, heads:%d, secs:%d\n",
          drive, d->cyls, d->heads, d->secs);
#endif

  state.status = TRS_HARD_READY | TRS_HARD_SEEKDONE;
  return 0;
}

/*
 * Check whether the current position is in bounds for the geometry.
 * If not, set the controller error status and return -1.  If so, fseek
 * the file to the start of the current sector and set the controller
 * status to newstatus and return 0.
 */
static int hard_sector(int newstatus)
{
  const HardImage *d = &hard_slot[state.drive];

  if (d->file == NULL && hard_open(state.drive) != 0) return -1;

  if (/**state.cyl >= d->cyls ||**/ /* ignore this limit */
      state.head >= d->heads ||
      state.secnum > d->secs /* allow 0-origin or 1-origin */ ) {
    error("hard%d: requested cyl:%d, head:%d, sec:%d (cyls:%d, heads:%d, secs:%d)",
        state.drive, state.cyl, state.head, state.secnum, d->cyls, d->heads, d->secs);
    hard_error(TRS_HARD_NFERR);
    return -1;
  }

  if (d->file && fseek(d->file,
      hard_image_offset(d, state.secsize, state.cyl, state.head,
                        state.secnum % d->secs), 0) != 0) {
      file_error("hard%d: fseek '%s'", state.drive, d->filename);
      hard_error(TRS_HARD_NFERR);
      return -1;
  }

  state.status = newstatus;

  if (trs_show_led)
    trs_hard_led(state.drive, 1);

  return 0;
}

static int hard_data_in(void)
{
  if ((state.command & TRS_HARD_CMDMASK) == TRS_HARD_READ &&
      (state.status & TRS_HARD_ERR) == 0) {
    if (state.bytesdone < state.secsize) {
      state.status &= ~TRS_HARD_DRQ;
      return state.secbuf[state.bytesdone++];
    }
  }
  return 0;
}

static void hard_data_out(int value)
{
  if (state.bytesdone < state.secsize) {
    state.secbuf[state.bytesdone] = value;
    state.bytesdone++;
    if ((state.bytesdone == state.secsize) &&
        (state.command & TRS_HARD_CMDMASK) == TRS_HARD_WRITE &&
        (state.status & TRS_HARD_ERR) == 0) {
      FILE *f = hard_slot[state.drive].file;

      /* Drop DRQ */
      state.status &= ~TRS_HARD_DRQ;
      if (f && fwrite(state.secbuf, 1, state.secsize, f) != state.secsize) {
        if (errno) {
          file_error("writing hard%d", state.drive);
          hard_error(TRS_HARD_DATAERR); /* arbitrary choice */
        }
      }
#if ZBX
      if (trs_io_debug_flags & HARDDEBUG2)
        sector_buf("write");
#endif
    }
  }
}

void trs_hard_save(FILE *file)
{
  trs_save_uint8(file, &state.control, 1);
  trs_save_uint8(file, &state.error, 1);
  trs_save_uint8(file, &state.seccnt, 1);
  trs_save_uint8(file, &state.secnum, 1);
  trs_save_uint16(file, &state.secsize);
  trs_save_uint16(file, &state.cyl);
  trs_save_uint8(file, &state.drive, 1);
  trs_save_uint8(file, &state.head, 1);
  trs_save_uint8(file, &state.sdh, 1);
  trs_save_uint8(file, &state.status, 1);
  trs_save_uint8(file, &state.command, 1);
  trs_save_uint8(file, state.secbuf, SECTORSIZE);
  trs_save_int(file, &state.bytesdone, 1);
}

void trs_hard_load(FILE *file)
{
  trs_load_uint8(file, &state.control, 1);
  trs_load_uint8(file, &state.error, 1);
  trs_load_uint8(file, &state.seccnt, 1);
  trs_load_uint8(file, &state.secnum, 1);
  trs_load_uint16(file, &state.secsize);
  trs_load_uint16(file, &state.cyl);
  trs_load_uint8(file, &state.drive, 1);
  trs_load_uint8(file, &state.head, 1);
  trs_load_uint8(file, &state.sdh, 1);
  trs_load_uint8(file, &state.status, 1);
  trs_load_uint8(file, &state.command, 1);
  trs_load_uint8(file, state.secbuf, SECTORSIZE);
  trs_load_int(file, &state.bytesdone, 1);
}
