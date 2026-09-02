/* SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2026 Egbert H. Schroeer
 *
 * Hard-disk slots.
 *
 * A slot holds a disk image, not a controller.  The emulator has three
 * hard-disk controller backends — WD1000/1010 (trs_hard.c), OMTI 5527
 * (trs_omti.c) and Xebec S1410 (trs_xebec.c) — but they are three ways of
 * reaching the same disks, not three sets of disks.  A real Genie IIIs has
 * exactly one controller fitted and the operating system on the disk
 * decides which one it addresses; the user has no reason to say it again.
 * So the GUI, config and write-protect code deal in slot numbers alone,
 * and these helpers keep the backends in step.
 *
 * The three controllers sit at fixed, disjoint port ranges (WD1000
 * 0x50-0x57, OMTI 0x40-0x43, Xebec 0x00-0x02 on the TCS onboard SASI
 * adapter) and are wired in independently of one another and of the boot
 * EPROM, so each answers on its own ports whenever a disk is fitted.
 * Gating I/O on a user-selected controller was bug #6.
 *
 * Slot count is the WD1000's, the largest of the three.  The SASI
 * controllers reach only units 0 and 1, which their own 1-bit LUN already
 * enforces — nothing here needs to know that.
 *
 * Original work for sdltrs-MultiHDC. Not derived from xtrs, SDLTRS,
 * or SDL2TRS. See LICENSE for the full BSD 2-Clause text.
 */

#ifndef _TRS_HDCTL_H
#define _TRS_HDCTL_H

/* True if a menu-row type is the hard-disk one (as opposed to floppy,
   wafer or cassette). */
extern int  hdctl_is_hard_type(int type);

/* Number of hard-disk slots the machine has. */
extern int  hdctl_maxdrives(void);

/*
 * True if only the WD1000 can reach this slot.  The two SASI controllers
 * address units 0 and 1 only -- their LUN field is one bit wide -- so a
 * disk in a higher slot is invisible to a guest driving one of them.
 */
extern int  hdctl_slot_wd1000_only(int unit);

extern void hdctl_attach(int unit, const char *filename);
extern void hdctl_remove(int unit);
extern const char *hdctl_getfilename(int unit);
extern int  hdctl_getwriteprotect(int unit);
extern void hdctl_getgeometry(int unit, int *cyls, int *heads, int *secs);

#endif /* _TRS_HDCTL_H */
