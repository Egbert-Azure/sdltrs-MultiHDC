/*
 * Copyright (c) 2026, Egbert Schroeer
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Emulation of the Xebec S1410/S1410A SASI/MFM hard disk controller. See
 * trs_xebec.h for the port map / status bit sourcing (confirmed against
 * Thomas Holte's hd2.mac).
 *
 * Command set taken from the Xebec S1410A Owner's Manual: a SELECT
 * strobe, a 6-byte Device Control Block (DCB) sent one byte at a time
 * gated by the REQ status bit, a data phase (sector data for READ/WRITE,
 * 4 bytes for REQUEST SENSE STATUS, or an 8-byte parameter block for
 * INITIALIZE DRIVE CHARACTERISTICS), and a two-byte completion status
 * read back from the data port. Unlike OMTI, drives are addressed by a
 * single flat logical block number (DCB bytes 1-3) rather than raw
 * cylinder/head/sector; this emulator converts that to a file offset
 * using the attached image's own geometry, same as OMTI/WD1000.
 *
 * Disk images use the same 256-byte Reed header (reed.h) as trs_hard.c
 * and trs_omti.c. As with OMTI, the manual gives no live sector-size
 * register, so a fixed 512 bytes/sector (typical of the ST-506/MFM
 * drives the S1410A was paired with) is used unconditionally.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "error.h"
#include "reed.h"
#include "trs.h"
#include "trs_hard_image.h"
#include "trs_xebec.h"
#include "trs_state_save.h"

#define XEBECDEBUG1 (1 << 4)  /* show detail on all port i/o */
#define XEBECDEBUG2 (1 << 5)  /* show all commands */

#define XEBEC_SEC_PER_TRK 32     /* fallback if the header omits head count */
#define XEBEC_MAXHEADS 8
#define XEBEC_DEFAULT_SECSIZE 512
#define XEBEC_SECBUFSIZE 1024

/*
 * Composite status-register values for each phase, built from the
 * REQ/BUSY/CD/IO bits in trs_xebec.h. Standard SASI phase encoding:
 * C/D and I/O together select the phase (host writes command bytes when
 * CD is set and IO is clear; data flows host->controller when both are
 * clear, controller->host when IO is set; status is CD+IO together).
 */
#define XEBEC_STATUS_IDLE     0x00
#define XEBEC_STATUS_DCB      (TRS_XEBEC_ST_BUSY | TRS_XEBEC_ST_REQ | TRS_XEBEC_ST_CD)
#define XEBEC_STATUS_DATA_OUT (TRS_XEBEC_ST_BUSY | TRS_XEBEC_ST_REQ)
#define XEBEC_STATUS_DATA_IN  (TRS_XEBEC_ST_BUSY | TRS_XEBEC_ST_REQ | TRS_XEBEC_ST_IO)
#define XEBEC_STATUS_STATUS   (TRS_XEBEC_ST_BUSY | TRS_XEBEC_ST_REQ | TRS_XEBEC_ST_CD | TRS_XEBEC_ST_IO)

typedef enum {
  XEBEC_PH_IDLE,
  XEBEC_PH_DCB,
  XEBEC_PH_DATA_IN,
  XEBEC_PH_DATA_OUT,
  XEBEC_PH_STATUS
} XebecPhase;

/* One drive (LUN); geometry decoding is shared (trs_hard_image.h) */

/* Structure describing controller state */
typedef struct {
  XebecPhase phase;
  Uint8 status;

  Uint8 dcb[TRS_XEBEC_DCBLEN];
  int dcb_index;
  Uint8 command;
  int lun;

  Uint8 buf[XEBEC_SECBUFSIZE];
  Uint8 fillbuf[XEBEC_SECBUFSIZE];
  int bytesdone;
  int datalen;
  int secsize;
  int blocks;           /* sectors left in the current READ/WRITE (DCB byte 4) */
  Uint8 busdata;        /* last byte written to the bus while idle (selection ID echo) */
  Uint8 final_status;   /* next-to-last status byte */
  int status_index;     /* 0 = next-to-last byte pending, 1 = last (zero) byte pending */

  /* Latched for the next REQUEST SENSE (cleared at the start of every
   * other command, as a real controller does). */
  Uint8 sense_code;     /* TRS_XEBEC_ERR_* of the last completed command */
  int sense_valid;      /* sense_lba meaningful (address-valid bit) */
  long sense_lba;       /* block in error, or one past the last block formatted */

} State;

static State state;

static int  xebec_open(int drive);
static int  xebec_seek(int lun, long lba);
static long xebec_capacity(int lun);
static int  xebec_format(int lun, long lba, int to_end_of_drive);
static void xebec_command(void);
static void xebec_finish(int ok);
static void xebec_fail(int code, int addr_valid, long lba);
static int  xebec_data_in(void);
static void xebec_data_out(int value);

#ifdef ZBX
void trs_xebec_debug(void)
{
  int i;

  printf("Xebec hard disk controller state:");
  if (hard_image_present() == 0) {
    puts(" DISABLED");
    return;
  }

  printf("\n  phase:%d, lun:%d, command:0x%02X, status:0x%02X, secsize:%d\n",
      state.phase, state.lun, state.command, state.status, state.secsize);

  for (i = 0; i < TRS_XEBEC_MAXDRIVES; i++) {
    if (hard_slot[i].file) {
      printf("\nxebec%d: '%s'\n", i, hard_slot[i].filename);
      printf("\theads %d, cyls %4d, secs %4d, writeprot %d\n",
          hard_slot[i].heads, hard_slot[i].cyls, hard_slot[i].secs,
          hard_slot[i].writeprot);
    }
  }
}
#endif

/* Powerup or reset button */
void trs_xebec_init(int poweron)
{
  state.phase = XEBEC_PH_IDLE;
  state.status = XEBEC_STATUS_IDLE;
  state.dcb_index = 0;
  state.command = 0;
  state.lun = 0;
  state.bytesdone = 0;
  state.datalen = 0;
  state.blocks = 0;
  state.busdata = 0;
  state.final_status = 0;
  state.status_index = 0;
  memset(state.buf, 0, sizeof(state.buf));

  if (poweron) {
    int i;

    state.secsize = XEBEC_DEFAULT_SECSIZE;
    memset(state.fillbuf, TRS_XEBEC_FORMAT_FILL, sizeof(state.fillbuf));

    for (i = 0; i < TRS_XEBEC_MAXDRIVES; i++) {
      hard_slot[i].writeprot = 0;
      hard_slot[i].cyls = 0;
      hard_slot[i].heads = 0;
      hard_slot[i].secs = 0;

      xebec_open(i);
    }
  }
}

void trs_xebec_attach(int drive, const char *diskname)
{
  snprintf(hard_slot[drive].filename, FILENAME_MAX, "%s", diskname);

  if (xebec_open(drive) != 0)
    trs_xebec_remove(drive);
}

void trs_xebec_remove(int drive)
{
  hard_slot_remove(drive);
}

const char*
trs_xebec_getfilename(int unit)
{
  return hard_slot[unit].filename;
}

int
trs_xebec_getwriteprotect(int unit)
{
  return hard_slot[unit].writeprot;
}

void
trs_xebec_getgeometry(int unit, int *cyls, int *head, int *secs)
{
  if (hard_slot[unit].file) {
    *cyls = hard_slot[unit].cyls;
    *head = hard_slot[unit].heads;
    *secs = hard_slot[unit].secs;
  }
}

/*
 * TCS Genie IIIs onboard host adapter at ports 0x00-0x02 (see
 * trs_xebec.h): same controller, rawer bus interface. GDOS 2.4's driver
 * selects by writing the controller ID to the data port, verifying the
 * bus by reading it back, then strobing SEL and polling for BUSY.
 */
int trs_xebec_tcs_in(int port)
{
  int v = 0xff;

  if (hard_image_present()) {
    switch (port) {
    case TRS_XEBEC_TCS_DATA:
      v = state.phase == XEBEC_PH_IDLE ? state.busdata : xebec_data_in();
      break;
    case TRS_XEBEC_TCS_CTRL:
      v = state.status;
      break;
    }
  }
#if ZBX
  if (trs_io_debug_flags & XEBECDEBUG1)
    debug("[PC=%04X] trs_xebec_tcs_in(%02X) => %02X\n", Z80_PC, port, v);
#endif
  return v;
}

void trs_xebec_tcs_out(int port, int value)
{
#if ZBX
  if (trs_io_debug_flags & XEBECDEBUG1)
    debug("[PC=%04X] trs_xebec_tcs_out(%02X), %02X\n", Z80_PC, port, value);
#endif
  /* No image attached: no card in the machine, so ignore writes -- the
   * matching reads in trs_xebec_tcs_in() already float high. */
  if (hard_image_present() == 0)
    return;

  switch (port) {
  case TRS_XEBEC_TCS_DATA:
    if (state.phase == XEBEC_PH_IDLE)
      state.busdata = (Uint8)value;
    else
      xebec_data_out(value);
    break;
  case TRS_XEBEC_TCS_CTRL:
    /* Bus release/deselect */
    trs_xebec_init(0);
    break;
  case TRS_XEBEC_TCS_SEL:
    /* Respond to selection only when addressed as controller 0 (data
     * bus bit 0), the only ID GDOS's driver ever selects. */
    if (state.phase == XEBEC_PH_IDLE && (state.busdata & 0x01)) {
      state.secsize = TRS_XEBEC_TCS_SECSIZE;
      state.phase = XEBEC_PH_DCB;
      state.dcb_index = 0;
      state.status = XEBEC_STATUS_DCB;
    }
    break;
  }
}

static void xebec_finish(int ok)
{
  state.phase = XEBEC_PH_STATUS;
  state.status = XEBEC_STATUS_STATUS;
  state.final_status = ok == 0 ? 0 : TRS_XEBEC_ST_ERROR;
  state.status_index = 0;
}

/* Fail the current command and latch why, for the REQUEST SENSE that a
 * driver is expected to issue immediately afterwards. */
static void xebec_fail(int code, int addr_valid, long lba)
{
  state.sense_code = (Uint8)code;
  state.sense_valid = addr_valid;
  state.sense_lba = lba;
#if ZBX
  if (trs_io_debug_flags & XEBECDEBUG2)
    debug("trs_xebec: command 0x%02X FAILED, sense 0x%02X at lba %ld\n",
        state.command, state.sense_code, addr_valid ? lba : -1L);
#endif
  xebec_finish(-1);
}

/*
 * Total addressable blocks on a unit, from its Reed-header geometry.
 *
 * Only the format and check-track commands bound themselves by this.
 * READ/WRITE deliberately do not: they never have, and the verified
 * GDOS 2.4 path must not start seeing address errors it never saw.
 * The format commands do need the bound -- without it a guest whose
 * configured drive type is larger than the image just walks off the end,
 * and fseek+fwrite silently grow the .hdv (this is what inflated
 * "Kaempf CP-M-3-10mb.hdv" from 10 MB to 100 MB).
 */
static long xebec_capacity(int lun)
{
  const HardImage *d = &hard_slot[lun];

  return (long)d->cyls * d->heads * d->secs;
}

/*
 * Write the format data pattern over whole tracks.
 *
 * FORMAT TRACK / FORMAT BAD TRACK do a single track; FORMAT DRIVE runs
 * from the given block through to the end of the drive. Sector data is
 * all this emulator has -- there are no ID fields or ECC in a flat .hdv
 * -- so formatting is exactly this fill.
 *
 * The manual has a real controller round the start down to a track
 * boundary (4.5.3.5), using the geometry the host gave it in INITIALIZE
 * DRIVE CHARACTERISTICS. This emulator keys geometry to the image's Reed
 * header instead, so the guest's idea of a track and ours can differ --
 * Klaus Kaempf's CP/M formatter steps 16 sectors per track through an
 * image whose header declares 17. Rounding to *our* boundary would then
 * re-format block 0 forever instead of advancing. So we format
 * d->secs blocks from exactly where the guest asked: any mismatch
 * over-covers into the next track with the identical fill byte, which is
 * harmless and idempotent, where rounding could leave sectors untouched.
 *
 * On success state.sense_lba is left one block past the last block
 * written, which is what a driver walking the disk track by track reads
 * back with REQUEST SENSE (manual 4.5.3.4).
 *
 * Returns 0 on success, or a TRS_XEBEC_ERR_* code on failure.
 */
static int xebec_format(int lun, long lba, int to_end_of_drive)
{
  HardImage *d = &hard_slot[lun];
  const Uint8 *pattern;
  long first, last, blk;

  if (d->file == NULL && xebec_open(lun) != 0)
    return TRS_XEBEC_ERR_NOT_READY;
  if (d->secs <= 0 || xebec_capacity(lun) <= 0)
    return TRS_XEBEC_ERR_NOT_READY;
  if (d->writeprot)
    return TRS_XEBEC_ERR_WRITE_FAULT;

  first = lba;
  if (first >= xebec_capacity(lun))
    return TRS_XEBEC_ERR_BAD_ADDR;

  last = to_end_of_drive ? xebec_capacity(lun) : first + d->secs;
  if (last > xebec_capacity(lun))
    last = xebec_capacity(lun);

  /* Control byte bit 5: format with the buffer the host loaded via
   * WRITE SECTOR BUFFER rather than the controller's own fill byte. */
  pattern = (state.dcb[TRS_XEBEC_DCB_CONTROL] & TRS_XEBEC_CTRL_KEEPBUF)
          ? state.buf : state.fillbuf;

  /* Blocks are contiguous in the image, so one seek covers the range. */
  if (xebec_seek(lun, first) != 0)
    return TRS_XEBEC_ERR_NOT_READY;

  for (blk = first; blk < last; blk++) {
    if (fwrite(pattern, 1, state.secsize, d->file) != (size_t)state.secsize) {
      file_error("formatting xebec%d", lun);
      state.sense_lba = blk;
      return TRS_XEBEC_ERR_WRITE_FAULT;
    }
  }

  fflush(d->file);
  state.sense_lba = last;
  return TRS_XEBEC_ERR_NONE;
}

static void xebec_command(void)
{
  long lba;

  state.command = state.dcb[0];
  state.lun     = (state.dcb[1] & TRS_XEBEC_DCB1_LUNMASK) >> TRS_XEBEC_DCB1_LUNSHIFT;
  lba           = ((long)(state.dcb[1] & TRS_XEBEC_DCB1_ADDRMASK) << 16)
                | ((long)state.dcb[2] << 8)
                | state.dcb[3];

  state.bytesdone = 0;

#if ZBX
  if (trs_io_debug_flags & XEBECDEBUG2)
    /* Whole DCB, not just the decoded fields: byte 4 is the block count
     * for READ/WRITE but the interleave for the format commands, and
     * byte 5 is the control byte -- both are needed to tell what a guest
     * formatter is actually asking for. */
    debug("trs_xebec: command 0x%02X lun:%d lba:%ld"
          " dcb:%02X %02X %02X %02X %02X %02X secsize:%d\n",
        state.command, state.lun, lba,
        state.dcb[0], state.dcb[1], state.dcb[2],
        state.dcb[3], state.dcb[4], state.dcb[5], state.secsize);
#endif

  /* Sense describes the command just completed, so every command but
   * REQUEST SENSE itself starts by clearing it. */
  if (state.command != TRS_XEBEC_REQUEST_SENSE) {
    state.sense_code = TRS_XEBEC_ERR_NONE;
    state.sense_valid = 0;
    state.sense_lba = 0;
  }

  /* DCB byte 4 is the block count for READ/WRITE (0 means 256) */
  state.blocks = state.dcb[4] ? state.dcb[4] : 256;

  switch (state.command) {
  case TRS_XEBEC_READ:
    if (xebec_seek(state.lun, lba) == 0) {
      FILE *f = hard_slot[state.lun].file;

      if (f && fread(state.buf, 1, state.secsize, f) != (size_t)state.secsize) {
        if (ferror(f)) {
          file_error("reading xebec%d", state.lun);
          xebec_finish(-1);
          break;
        }
      }
      state.datalen = state.secsize;
      state.phase = XEBEC_PH_DATA_IN;
      state.status = XEBEC_STATUS_DATA_IN;
    } else {
      xebec_finish(-1);
    }
    break;

  case TRS_XEBEC_WRITE:
    if (xebec_seek(state.lun, lba) == 0) {
      state.datalen = state.secsize;
      state.phase = XEBEC_PH_DATA_OUT;
      state.status = XEBEC_STATUS_DATA_OUT;
    } else {
      xebec_finish(-1);
    }
    break;

  case TRS_XEBEC_FORMAT_DRIVE:
  case TRS_XEBEC_FORMAT_TRACK:
  case TRS_XEBEC_FORMAT_BAD_TRACK: {
    /* FORMAT DRIVE formats from the starting track to the end of the
     * disk; the two track commands do one track. FORMAT BAD TRACK only
     * differs on real media by setting the bad-track flag in the ID
     * field, which a flat .hdv has no room for. */
    int err = xebec_format(state.lun, lba,
                           state.command == TRS_XEBEC_FORMAT_DRIVE);

    if (err == TRS_XEBEC_ERR_NONE) {
      state.sense_valid = 1;   /* address = one past the last block done */
      xebec_finish(0);
    } else {
      xebec_fail(err, 1, state.sense_lba);
    }
    break;
  }

  case TRS_XEBEC_CHECK_TRACK:
    /* Verify a track's ID fields against the interleave table. A flat
     * image has no ID fields and cannot be unformatted, so any track
     * inside the drive's geometry checks out. Sense reports one block
     * past the checked track, as after a format. */
    if (hard_slot[state.lun].file == NULL && xebec_open(state.lun) != 0) {
      xebec_fail(TRS_XEBEC_ERR_NOT_READY, 0, 0);
    } else if (lba >= xebec_capacity(state.lun)) {
      xebec_fail(TRS_XEBEC_ERR_BAD_ADDR, 1, lba);
    } else {
      state.sense_lba = lba + hard_slot[state.lun].secs;
      state.sense_valid = 1;
      xebec_finish(0);
    }
    break;

  case TRS_XEBEC_READ_VERIFY:
    /* READ with no data passed to the host. Deliberately no range check:
     * READ and WRITE do not range-check either, and this must not be the
     * one command that starts rejecting addresses the working GDOS 2.4
     * path may rely on. */
    xebec_finish(xebec_seek(state.lun, lba) == 0 ? 0 : -1);
    break;

  case TRS_XEBEC_READ_ECC_LEN:
    /* Nothing is ever corrected by ECC here, so the burst length is
     * always zero. */
    state.buf[0] = 0;
    state.datalen = 1;
    state.phase = XEBEC_PH_DATA_IN;
    state.status = XEBEC_STATUS_DATA_IN;
    break;

  case TRS_XEBEC_INIT_DRIVE_CHAR:
    state.datalen = TRS_XEBEC_CHARLEN;
    state.phase = XEBEC_PH_DATA_OUT;
    state.status = XEBEC_STATUS_DATA_OUT;
    break;

  case TRS_XEBEC_SEEK:
    xebec_finish(xebec_seek(state.lun, lba));
    break;

  case TRS_XEBEC_RECALIBRATE:
    xebec_finish(xebec_seek(state.lun, 0));
    break;

  case TRS_XEBEC_TEST_DRIVE_READY:
    xebec_finish(hard_slot[state.lun].file != NULL ||
                xebec_open(state.lun) == 0 ? 0 : -1);
    break;

  case TRS_XEBEC_REQUEST_SENSE:
    /* Report what the previous command latched. Byte 0 carries the error
     * code plus the address-valid bit, bytes 1-3 the block address (with
     * the LUN in bit 5 of byte 1). */
    state.buf[0] = state.sense_code |
        (state.sense_valid ? TRS_XEBEC_SENSE_ADDRVALID : 0);
    state.buf[1] = (Uint8)(((state.sense_lba >> 16) & TRS_XEBEC_DCB1_ADDRMASK) |
        (state.lun << TRS_XEBEC_DCB1_LUNSHIFT));
    state.buf[2] = (Uint8)(state.sense_lba >> 8);
    state.buf[3] = (Uint8)state.sense_lba;
    state.datalen = TRS_XEBEC_SENSELEN;
    state.phase = XEBEC_PH_DATA_IN;
    state.status = XEBEC_STATUS_DATA_IN;
    break;

  case TRS_XEBEC_READ_BUFFER:
  case TRS_XEBEC_READ_BUFFER_S1410:
    /* Return the sector buffer as-is, no disk access */
    state.datalen = state.secsize;
    state.phase = XEBEC_PH_DATA_IN;
    state.status = XEBEC_STATUS_DATA_IN;
    break;

  case TRS_XEBEC_WRITE_BUFFER:
    /* Fill the sector buffer, no disk access */
    state.datalen = state.secsize;
    state.phase = XEBEC_PH_DATA_OUT;
    state.status = XEBEC_STATUS_DATA_OUT;
    break;

  default:
    error("trs_xebec: unknown command 0x%02X", state.command);
    xebec_fail(TRS_XEBEC_ERR_BAD_CMD, 0, 0);
    break;
  }
}

static void xebec_data_out(int value)
{
  switch (state.phase) {
  case XEBEC_PH_DCB:
    state.dcb[state.dcb_index++] = (Uint8)value;
    if (state.dcb_index == TRS_XEBEC_DCBLEN)
      xebec_command();
    break;

  case XEBEC_PH_DATA_OUT:
    if (state.bytesdone < state.datalen) {
      state.buf[state.bytesdone++] = (Uint8)value;
      if (state.bytesdone == state.datalen) {
        if (state.command == TRS_XEBEC_WRITE) {
          FILE *f = hard_slot[state.lun].file;

          if (f && fwrite(state.buf, 1, state.secsize, f) != (size_t)state.secsize) {
            if (errno) {
              file_error("writing xebec%d", state.lun);
              xebec_finish(-1);
              break;
            }
          }
          if (--state.blocks > 0) {
            /* More sectors in this command: keep the data phase open,
             * file position is already at the next sector */
            state.bytesdone = 0;
            break;
          }
        }
        /* INITIALIZE DRIVE CHARACTERISTICS is acknowledged but does not
         * change addressing geometry: as with trs_omti.c's SET DRIVE
         * CHARACTERISTICS, real geometry stays keyed to the attached
         * image's own Reed header (the actual physical disk), not to
         * whatever a boot ROM/driver declares here. It is worth tracing
         * though -- a guest declaring a bigger drive than the image is
         * exactly what walks a formatter off the end of the .hdv. */
#if ZBX
        if (state.command == TRS_XEBEC_INIT_DRIVE_CHAR &&
            (trs_io_debug_flags & XEBECDEBUG2))
          debug("trs_xebec: drive characteristics: %d cyls, %d heads"
                " (image says %d cyls, %d heads, %d sec/trk)\n",
              (state.buf[TRS_XEBEC_CHAR_CYLHI] << 8) |
                  state.buf[TRS_XEBEC_CHAR_CYLLO],
              state.buf[TRS_XEBEC_CHAR_HEADS],
              hard_slot[state.lun].cyls, hard_slot[state.lun].heads,
              hard_slot[state.lun].secs);
#endif
        xebec_finish(0);
      }
    }
    break;

  default:
    break;
  }
}

static int xebec_data_in(void)
{
  switch (state.phase) {
  case XEBEC_PH_DATA_IN:
    if (state.bytesdone < state.datalen) {
      int v = state.buf[state.bytesdone++];

      if (state.bytesdone == state.datalen) {
        if (state.command == TRS_XEBEC_READ && --state.blocks > 0) {
          /* More sectors in this command: refill the buffer from the
           * next sector, file position is already there */
          FILE *f = hard_slot[state.lun].file;

          if (f && fread(state.buf, 1, state.secsize, f) != (size_t)state.secsize) {
            if (ferror(f)) {
              file_error("reading xebec%d", state.lun);
              xebec_finish(-1);
              return v;
            }
          }
          state.bytesdone = 0;
        } else {
          xebec_finish(0);
        }
      }
      return v;
    }
    break;

  case XEBEC_PH_STATUS: {
    /* Two bytes of completion status are passed to the host: the
     * next-to-last (error+LUN) byte, then a final zero byte that signals
     * "done". Reading the last byte returns the bus to idle. */
    int v = state.status_index == 0 ? state.final_status : 0;

    if (state.status_index == 0) {
      state.status_index = 1;
    } else {
      trs_xebec_init(0);
    }
    return v;
  }

  default:
    break;
  }
  return 0xff;
}

/*
 * Convert a flat logical block address to cyl/head/sector using the
 * attached drive's geometry, then position the file at the start of
 * that sector. Returns 0 if OK, -1 otherwise.
 */
static int xebec_seek(int lun, long lba)
{
  HardImage *d = &hard_slot[lun];
  long cyl, head, sector;

  if (d->file == NULL && xebec_open(lun) != 0) return -1;

  sector = lba % d->secs;
  cyl    = lba / d->secs;
  head   = cyl % d->heads;
  cyl    = cyl / d->heads;

  if (d->file && fseek(d->file,
      hard_image_offset(d, state.secsize, cyl, head, sector), 0) != 0) {
    file_error("xebec%d: fseek '%s'", lun, d->filename);
    return -1;
  }

  if (trs_show_led)
    trs_hard_led(lun, 1);

  return 0;
}

/*
 * Open (if needed) the image for a drive, parse its Reed header, and
 * derive geometry. As with trs_omti.c, this protocol has no live
 * sector-size register, so a fixed size is used unconditionally.
 */
static int xebec_open(int drive)
{
  HardImage *d = &hard_slot[drive];

  if (hard_image_open(d, drive, "xebec",
                      XEBEC_SEC_PER_TRK, XEBEC_MAXHEADS) != 0)
    return -1;

  state.status = XEBEC_STATUS_IDLE;
  return 0;
}

void trs_xebec_save(FILE *file)
{
  int phase = (int)state.phase;

  trs_save_int(file, &phase, 1);
  trs_save_uint8(file, &state.status, 1);
  trs_save_uint8(file, state.dcb, TRS_XEBEC_DCBLEN);
  trs_save_int(file, &state.dcb_index, 1);
  trs_save_uint8(file, &state.command, 1);
  trs_save_int(file, &state.lun, 1);
  trs_save_uint8(file, state.buf, XEBEC_SECBUFSIZE);
  trs_save_uint8(file, state.fillbuf, XEBEC_SECBUFSIZE);
  trs_save_int(file, &state.bytesdone, 1);
  trs_save_int(file, &state.datalen, 1);
  trs_save_int(file, &state.secsize, 1);
  trs_save_int(file, &state.blocks, 1);
  trs_save_uint8(file, &state.busdata, 1);
  trs_save_uint8(file, &state.final_status, 1);
  trs_save_int(file, &state.status_index, 1);
}

void trs_xebec_load(FILE *file)
{
  int phase;

  trs_load_int(file, &phase, 1);
  state.phase = (XebecPhase)phase;
  trs_load_uint8(file, &state.status, 1);
  trs_load_uint8(file, state.dcb, TRS_XEBEC_DCBLEN);
  trs_load_int(file, &state.dcb_index, 1);
  trs_load_uint8(file, &state.command, 1);
  trs_load_int(file, &state.lun, 1);
  trs_load_uint8(file, state.buf, XEBEC_SECBUFSIZE);
  trs_load_uint8(file, state.fillbuf, XEBEC_SECBUFSIZE);
  trs_load_int(file, &state.bytesdone, 1);
  trs_load_int(file, &state.datalen, 1);
  trs_load_int(file, &state.secsize, 1);
  trs_load_int(file, &state.blocks, 1);
  trs_load_uint8(file, &state.busdata, 1);
  trs_load_uint8(file, &state.final_status, 1);
  trs_load_int(file, &state.status_index, 1);

}
