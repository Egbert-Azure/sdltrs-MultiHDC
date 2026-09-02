<!-- docs/reference/gdos-2.4-addresses.md -->
# GDOS 2.4 addresses

The names this project uses for addresses outside the module being read.
One table, so the disassembly listings and the driver's own source cannot
drift apart.

**The addresses are this project's own, for GDOS 2.4.** Grosser's book supplies
the naming convention and many of the meanings, but his addresses are
NEWDOS/80 2.1c and the routine entry points moved between versions -- his
`DRVSEL` is `445Bh`, ours is `4776h`. Only the 4000h-443Fh vector table is
stable enough to take an address from the book directly.

An address gets an entry only where a source gives it a meaning. Anything else
stays `mXXXX` in the listings rather than acquiring a plausible-sounding name.

| Address | Name | Meaning | Source |
|---|---|---|---|
| `0033` | `ROMCHR` | ROM: put character A on the screen | Grosser ch.3 / Volker Dose's sources |
| `0040` | `LINPUT` | ROM: read a line | Grosser, cited by name / Volker Dose's sources |
| `0049` | `ROMKEY` | ROM: wait for a key | Volker Dose's sources |
| `1914` | `SCRLEN` | screen line length | Volker Dose's sources |
| `3641` | `SCROFF` | offset of the visible from the physical screen | Volker Dose's sources |
| `3649` | `SCRPUT` | put character A on the screen at (HL) | Volker Dose's sources |
| `36FF` | `DBANKS` | banked-RAM size and flag bits, sized by the driver's ginit | this port's driver |
| `37CC` | `RPDRV` | relocated PDRIVE block | this port's driver |
| `37D6` | `DTABH` | drive table, high bytes | this port's driver |
| `37DF` | `DTAB` | drive table | this port's driver |
| `3A00` | `RSTUB` | low-RAM transfer stub | this port's driver |
| `4015` | `KBDTYP` | keyboard DCB: type | Grosser ch.3 / Volker Dose's sources |
| `4016` | `KBDDRV` | keyboard DCB: driver address | Volker Dose's sources |
| `401D` | `VIDTYP` | screen DCB: type | Volker Dose's sources |
| `401E` | `VIDDRV` | screen DCB: driver address | Volker Dose's sources |
| `4020` | `VIDCUR` | screen DCB: cursor address | Grosser ch.3 / Volker Dose's sources |
| `4022` | `CURFLG` | video DCB: cursor on/off | Grosser ch.3 / Volker Dose's sources |
| `4023` | `VIDTOP` | video DCB: number of header lines | Grosser ch.3 / Volker Dose's sources |
| `4024` | `VIDBOT` | video DCB: number of footer lines | Volker Dose's sources |
| `4025` | `PRTTYP` | printer DCB: type | Volker Dose's sources |
| `4026` | `PRTDRV` | printer DCB: driver address | Volker Dose's sources |
| `402D` | `DOSRDY` | return to the DOS prompt | Grosser, cited by name / Volker Dose's sources |
| `4030` | `ERRORO` | DOS error output | Grosser, cited by name |
| `4049` | `HIMEM` | HIMEM | Grosser ch.3 / Volker Dose's sources |
| `4063` | `HEXDE` | write DE as hex ASCII to (HL) | Volker Dose's sources |
| `4068` | `HEXA` | write A as hex ASCII to (HL) | Volker Dose's sources |
| `41E0` | `DOSSTK` | initial address of the DOS stack | Volker Dose's sources |
| `4200` | `SECBUF` | DOS sector buffer | Grosser ch.3 / Volker Dose's sources |
| `421F` | `DIRLEN` | length of the directory field | measured here |
| `42A0` | `DNFLOP` | number of floppy drives | this port's driver |
| `4306` | `DSAVE` | current drive, display copy | this port's driver |
| `4307` | `DMACH` | machine type; 04h is the Genie IIIs | measured here (SYS0/SYS 3214h, 3220h) |
| `4308` | `DDRIVE` | current drive | this port's driver |
| `4309` | `DMASK` | drive-select bit pattern for 37E1h | Grosser ch.3 / this port's driver |
| `430A` | `DPDRV` | PDRIVE parameters of the current drive | this port's driver |
| `4312` | `BRKVEC` | BREAK vector (RST 28h, A<20h) | Volker Dose's sources |
| `4317` | `DMODUL` | current /SYS module | Grosser ch.3 |
| `4318` | `DCMDBF` | DOS input buffer | Grosser ch.3 / Volker Dose's sources |
| `4369` | `DFLAG0` | DOS flags: DEBUG, CHAINING, BREAK key, RUN-ONLY (Grosser ch.3) | Grosser ch.3 |
| `436A` | `DFLAG1` | DOS operating-state flags | Grosser ch.3 / Volker Dose's sources |
| `436B` | `DFLAG2` | flags SYS6 manipulates for CLOSE and EXPAND | Grosser ch.3 / Volker Dose's sources |
| `436C` | `DFLAG3` | further DOS flags | Grosser ch.3 / Volker Dose's sources |
| `436D` | `DFLAG4` | further DOS flags | Grosser ch.3 / Volker Dose's sources |
| `4371` | `PDRV0` | start of the PDRIVE parameters for drive 0 | Volker Dose's sources |
| `4399` | `DPPTR` | pointer into the PDRIVE table | this port's driver |
| `439B` | `SPSAVE` | stack-pointer save slot | Volker Dose's sources |
| `439D` | `SPSAV2` | stack-pointer save slot, second | Volker Dose's sources |
| `439F` | `DNDRV` | number of drives | Volker Dose's sources |
| `43A0` | `DSYSAN` | SYSTEM AN: drive for DIR | Grosser ch.3 / Volker Dose's sources |
| `43A1` | `DSYSAO` | SYSTEM AO: drive for new files | measured here |
| `43A7` | `INPCH2` | first two characters of an input | Volker Dose's sources |
| `43AB` | `DINIT` | DOS initialised (A5h) | measured here |
| `43D4` | `DFCBDV2` | GETSYS FCB: drive | this port's driver |
| `43D5` | `DFCBDEC` | GETSYS FCB: DEC | this port's driver |
| `43D8` | `DFCBDV` | GETSYS FCB: NEXT, low byte | this port's driver |
| `4403` | `PARMBF` | parameter buffer for DOS-CALL, and the start address for LOAD | Grosser ch.3 |
| `4405` | `DOSCMD` | execute the DOS command at (HL) | Grosser, cited by name |
| `4408` | `ERRRET` | error exit; a plain RET when there is no error | Volker Dose's sources |
| `4409` | `DOSERR` | DOS error exit | Grosser, cited by name / Volker Dose's sources |
| `4419` | `DOSCAL` | DOS call | Grosser, cited by name |
| `4420` | `FINIT` | INIT: create the file if it does not exist | Volker Dose's sources |
| `4424` | `FOPEN` | OPEN: do not create a new file | Volker Dose's sources |
| `4430` | `LOAD` | load a program | Grosser, cited by name |
| `4436` | `READ` | read a sector | Grosser, cited by name |
| `4439` | `WRITE` | write a sector | Grosser, cited by name |
| `443C` | `VERIFY` | verify a sector | Grosser, cited by name |
| `4467` | `DSPLY` | display the text at (HL) | Grosser ch.3 / Volker Dose's sources |
| `446D` | `TIME` | read the clock | Grosser, cited by name |
| `4480` | `USRFCB` | FCB for loading and starting user programs | Volker Dose's sources |
| `448C` | `RSYSFCB` | GETSYS drive-0 landing pad, planted by `ginit` in the dead FFh run at 448Ch-449Fh: resets DFCBDV (NEXT) to 0, then loads A with SYSVOL and jumps to DRVSEL, keeping the two purposes `XOR A` used to serve apart | this port's driver |
| `4495` | `RDECFIX` | GETSYS landing pad, right behind RSYSFCB: persists the module DEC into DFCBDEC before making the stock GETFDE call | this port's driver |
| `4516` | `CHNTOG` | toggle the CHAINING flag in DFLAG0 | Grosser ch.3 (DFLAG0 bit 5) / read from the code: XOR 20h |
| `45B0` | `CONTLC` | continuation for SYSTEM,BG or the LC command | Grosser ch.3 / Volker Dose's sources |
| `45B5` | `UPCASE` | convert lower case to upper case | Volker Dose's sources |
| `4630` | `READS` | read a physical sector | Grosser ch.3 p.3-24 |
| `4634` | `TESTS` | verify a physical sector | Grosser ch.3, cited by name |
| `463C` | `WRITDS` | write a directory sector | Grosser ch.3 p.3-25 |
| `4640` | `WRITES` | write a physical sector | Grosser ch.3 p.3-25 |
| `4642` | `DXFER` | transfer entry, hooked by this port's driver | Grosser ch.3 / this port's driver |
| `4645` | `DXFERF` | transfer, floppy path | this port's driver |
| `46C4` | `DCMD` | last FDC command | this port's driver |
| `476E` | `DRVSLX` | DRVSEL with the drive taken from (IX+6) | read from the code: LD A,(IX+6) / JR DRVSEL |
| `4776` | `DRVSEL` | select a drive | Grosser ch.3 / Volker Dose's sources / this port's driver |
| `477C` | `DDRVSL` | DRVSEL entry, hooked by this port's driver | this port's driver |
| `4780` | `DDRVFL` | DRVSEL, floppy path | this port's driver |
| `4784` | `DDRVNR` | number of drives, for DRVSEL | this port's driver |
| `47DE` | `DDRVER` | DRVSEL, normal return | Grosser ch.3 / this port's driver |
| `47EC` | `DSKTST` | select the drive, motor on, test 'disk in ?' | Volker Dose's sources / read from the code: CALL DRVSEL / RET NZ |
| `47EF` | `DSKMNT` | wait for the index hole | this port's driver |
| `490A` | `DIRSEC` | read a sector from the directory | Volker Dose's sources |
| `492F` | `FDEGET` | fetch a file's FDE from the directory | Grosser ch.3 / Volker Dose's sources |
| `4936` | `GETFDE` | fetch a file's FDE from the directory, second entry | Volker Dose's sources / this port's driver |
| `494B` | `RDFPDE` | load the directory sector holding the FPDE (FCB+7) to 4200h, HL to FPDE+0 | Grosser ch.3 |
| `49CD` | `ERRXIT` | error exit, via the emergency exit | Grosser ch.3 |
| `49D3` | `SYSLD` | load a SYS file; exits on error | Volker Dose's sources |
| `49D6` | `SYSLD2` | load a SYS file; sets the no-error flag itself | Volker Dose's sources |
| `4BC9` | `GETSYS` | load and start a SYS module | Grosser, cited by name |
| `4C20` | `GSYSCL` | clear the GETSYS-running flag in DFLAG0 | Grosser ch.3 (DFLAG0 bit 6) / read from the code: RES 6,(HL) |
| `4C28` | `SYSLOAD` | load a SYS file | Grosser ch.3 |
| `4C88` | `DGRAN1` | sectors per GRAN | this port's driver |
| `4C92` | `MULHL` | HL * A | this port's driver |
| `4C94` | `MULOV` | HL * A, overflow | this port's driver |
| `4CB3` | `DGRAN2` | sectors per GRAN, second copy | this port's driver |
| `4CC5` | `STRCMP` | compare the strings at (HL) and (BC) | Volker Dose's sources |
| `4CD5` | `CHKCHR` | test the character at (HL) | Volker Dose's sources |
| `4CD9` | `CHKSEP` | check for a comma or a blank | Volker Dose's sources |
| `F00D` | `GWORK` | hard-disk driver's work vector | this port's driver |
| `F016` | `GDISP3` | hard-disk driver's dispatch table, slot 3 -- MEMDISK's hook | this port's driver |
| `F040` | `GSTK` | hard-disk driver's stack | this port's driver |

---
Copyright (c) 2026 Egbert H. Schroeer. BSD 2-Clause; see [LICENSE](../../LICENSE).
