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
 * Emulation of the Xebec S1410/S1410A SASI/MFM hard disk controller,
 * believed to be the controller genuinely used by the TCS Genie IIIs'
 * built-in hard disk (GDOS 2.4, Klaus Kaempf's CP/M port).
 *
 * Command set (class/opcode values) and the status-register bit layout are
 * taken from the real Xebec S1410A Owner's Manual. On the Genie IIIs the
 * Xebec is reached only through the TCS onboard SASI adapter at ports
 * 0x00-0x02 (used by GDOS 2.4 and, on the same convention, Holte's CP/M
 * port with the original EPROM). The 0x40-0x43 range belongs exclusively to
 * the OMTI controller (trs_omti.h), never the Xebec -- on real hardware only
 * one controller chip is ever fitted, but the two ranges are disjoint, so
 * trs_io.c's GENIE3S dispatch just routes each range to its own controller
 * and nothing arbitrates between them.
 *
 * DCB addressing (flat logical block number rather than OMTI's raw
 * cylinder/head/sector) and the two-byte completion status come from the
 * manual.
 */

#ifndef _TRS_XEBEC_H
#define _TRS_XEBEC_H

extern void  trs_xebec_init(int poweron);
extern void  trs_xebec_attach(int drive, const char *diskname);
extern void  trs_xebec_remove(int drive);
extern int   trs_xebec_tcs_in(int port);
extern void  trs_xebec_tcs_out(int port, int value);

#define TRS_XEBEC_MAXDRIVES 2 /* SASI 1-bit LUN (hd2.mac MAXDRIVE) addresses 2 units */

/*
 * Host-adapter interface: the TCS Genie IIIs' own onboard SASI adapter at
 * ports 0x00-0x02, as used by GDOS 2.4's resident hard-disk driver
 * (reverse-engineered from a live disassembly of the driver at F000h-F4FFh
 * on a booted G3S-GDOS24.DMK; selection routine at F1B6h). Selection is
 * done the real SASI way: the host writes the controller ID (01h) to the
 * data port, reads it back to verify the bus, then pulses SEL via port 2
 * and waits for the controller to assert BUSY. A write to port 1 releases
 * the bus. Data-phase transfers are auto-handshaked 256-byte INIR/OTIR
 * bursts (GDOS runs the S1410 with 256-byte sectors).
 */
#define TRS_XEBEC_TCS_DATA 0x00 /* SASI data bus (read/write) */
#define TRS_XEBEC_TCS_CTRL 0x01 /* read: status bits / write: deselect (bus release) */
#define TRS_XEBEC_TCS_SEL  0x02 /* SEL strobe (write only) */

#define TRS_XEBEC_TCS_SECSIZE 256

/*
 * Status register bits polled at TRS_XEBEC_TCS_CTRL, per hd2.mac's
 * REQMASK/BUSYMASK/CDMASK/IOMASK equates (which also match the Xebec
 * manual's own sample Z80 driver code, CDBIT/CDMASK/IOBIT/IOMASK).
 * Standard SASI phase encoding: C/D and I/O together select the phase.
 */
#define TRS_XEBEC_ST_REQ  0x01 /* request: a byte is ready to transfer */
#define TRS_XEBEC_ST_BUSY 0x02 /* controller busy (selected, mid-command) */
#define TRS_XEBEC_ST_CD   0x08 /* command/[data]: set during command and status phases */
#define TRS_XEBEC_ST_IO   0x10 /* [host->controller]/controller->host: set when controller is driving (data-in, status) */

/*
 * Device Control Block (DCB): 6 bytes, per Xebec S1410A manual section
 * "COMMANDS". Byte 0 top 3 bits are the command class; classes 1-6 are
 * reserved, so for the class 0 commands this emulator implements, byte 0
 * equals the opcode directly. Opcode values confirmed against hd2.mac's
 * own $TSTDRV/$REST/$STATUS/$$READ/$$WRITE/$INIDRV equates.
 */
#define TRS_XEBEC_DCBLEN 6

#define TRS_XEBEC_TEST_DRIVE_READY 0x00
#define TRS_XEBEC_RECALIBRATE      0x01
#define TRS_XEBEC_REQUEST_SENSE    0x03
#define TRS_XEBEC_FORMAT_DRIVE     0x04
#define TRS_XEBEC_CHECK_TRACK      0x05
#define TRS_XEBEC_FORMAT_TRACK     0x06
#define TRS_XEBEC_FORMAT_BAD_TRACK 0x07
#define TRS_XEBEC_READ             0x08
#define TRS_XEBEC_READ_VERIFY      0x09
#define TRS_XEBEC_WRITE            0x0a
#define TRS_XEBEC_SEEK             0x0b
#define TRS_XEBEC_INIT_DRIVE_CHAR  0x0c
#define TRS_XEBEC_READ_ECC_LEN     0x0d
#define TRS_XEBEC_READ_BUFFER      0x0e
#define TRS_XEBEC_WRITE_BUFFER     0x0f
/*
 * The S1410A manual numbers the sector-buffer pair 0x0F WRITE SECTOR
 * BUFFER / 0x10 READ SECTOR BUFFER, and gives 0x0E to FORMAT ALTERNATE
 * TRACK. TRS_XEBEC_READ_BUFFER above stays at 0x0E because that is what
 * GDOS 2.4's resident driver was observed to issue and what the verified
 * working GDOS path uses; 0x10 is accepted as the manual-correct synonym
 * so drivers written to the book also work. Resolve the discrepancy with
 * an XEBECDEBUG2 trace of a real GDOS session before changing 0x0E.
 */
#define TRS_XEBEC_READ_BUFFER_S1410 0x10

/* DCB byte 1: LUN bit and high 5 bits of the 21-bit logical block address */
#define TRS_XEBEC_DCB1_ADDRMASK 0x1f
#define TRS_XEBEC_DCB1_LUNMASK  0x20
#define TRS_XEBEC_DCB1_LUNSHIFT 5

/*
 * DCB byte 5 is the control byte. Bit 5 tells the controller to use the
 * host-supplied sector buffer (loaded with WRITE SECTOR BUFFER) as the
 * data pattern for the format commands instead of its own fill byte.
 */
#define TRS_XEBEC_DCB_CONTROL   5
#define TRS_XEBEC_CTRL_KEEPBUF  0x20

/*
 * Next-to-last completion status byte (first of the two bytes read back
 * from TRS_XEBEC_TCS_DATA once the status phase is reached). The second
 * (last) status byte is always zero -- it just signals "done" to the
 * host. Error bit confirmed against hd2.mac's own ERROR equate (02H).
 */
#define TRS_XEBEC_ST_ERROR 0x02

/*
 * Request Sense Status (opcode 0x03) reply: 4 bytes.
 * Byte 0: bits 0-3 error code, bits 4-5 error type, bit 7 address valid.
 * Bytes 1-3: logical block address (MSB first) associated with the error.
 *
 * Error codes below are the subset this emulator can actually produce;
 * the full table is in the manual, section 4.7.
 */
#define TRS_XEBEC_SENSELEN 4

#define TRS_XEBEC_SENSE_ADDRVALID 0x80

#define TRS_XEBEC_ERR_NONE        0x00 /* command completed OK              */
#define TRS_XEBEC_ERR_NOT_READY   0x04 /* drive not ready after selection   */
#define TRS_XEBEC_ERR_WRITE_FAULT 0x03 /* write fault (write-protected here)*/
#define TRS_XEBEC_ERR_FORMAT      0x1a /* format error (check track format) */
#define TRS_XEBEC_ERR_BAD_CMD     0x20 /* invalid command                   */
#define TRS_XEBEC_ERR_BAD_ADDR    0x21 /* illegal disk address              */

/*
 * Data pattern written into the DATA fields by the format commands when
 * the host does not supply its own (control-byte bit 5, above).
 *
 * The manual's default is 0x6C. This emulator writes 0xE5 instead: it is
 * what the verified-working GDOS 2.4 HDFORMAT path has always seen here,
 * and 0xE5 is also what CP/M expects to find in an erased directory. Try
 * 0x6C if a guest formatter turns out to depend on the documented value.
 */
#define TRS_XEBEC_FORMAT_FILL 0xe5

/*
 * Initialize Drive Characteristics (opcode 0x0C) parameter block: 8
 * bytes sent by the host after the DCB. Only cylinder count and head
 * count affect this emulator's addressing (mirroring trs_omti.c's
 * SET_CHARACTERISTICS: real geometry is keyed to the attached image's
 * own Reed header, not to whatever a boot ROM/driver declares here).
 */
#define TRS_XEBEC_CHARLEN 8
#define TRS_XEBEC_CHAR_CYLHI 0
#define TRS_XEBEC_CHAR_CYLLO 1
#define TRS_XEBEC_CHAR_HEADS 2

#endif /* _TRS_XEBEC_H */
