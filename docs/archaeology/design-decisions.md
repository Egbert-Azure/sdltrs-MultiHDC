<!-- /docs/archaeology/design-decisions.md -->

# Emulator design decisions

Decisions that are not obvious from the code, including the ones that were made twice. Each entry says what was chosen, what was rejected, and what evidence settled it — so that a future reader who thinks "surely this should work the other way" can find out whether it already did.

## There is no active controller

**Decided:** the emulator does not model which controller card is fitted. All three answer on their own fixed, disjoint ports whenever a disk is attached.

An earlier build did the opposite. `dc8c4e5` ("Enforce a single active hard-disk controller on the Genie IIIs bus") gated port I/O on a user-selected controller, with a selector in the Alt-H GUI and a `hardcontroller=` config key to persist it. It reasoned from the hardware: a real Genie IIIs has exactly one controller card in it, so the emulator should too.

That was **bug #6**, and `9bbdea4` reverted it. Two things were wrong with it:

- It solved a problem nobody has. The operating system on the disk already decides which controller it addresses. No Genie IIIs software probes across the port ranges and gets confused by a second controller, because on real hardware it never sees one. Modelling the exclusivity bought nothing and cost the user a setting they had no way to get right.
- It could **silently switch off a working disk**. A stale `hardcontroller=` in `~/.sdltrs.t8c` — from an earlier session, or a config written before the disk was attached — made an attached, valid image invisible with no error anywhere (`42273fd`). The setting was a trap.

`hardcontroller=` is still parsed so old configs load, and does nothing (`opt_hardctl()` in `trs_options.c`). The `-omti<n>` / `-xebec<n>` flags likewise still work and map onto the same controller-free `hard<n>` slots.

The pairing between boot EPROM and controller remains true as *guidance* — a mismatched ROM and disk still means the disk is not found — it is just not policed in code. See [`original-rom-behavior.md`](original-rom-behavior.md), which keeps the rejected cross-check proposal as a record.

## A slot holds a disk, not a controller

**Decided:** one set of four hard-disk slots for the machine, shared by all three backends.

Before `29ae362` and `9bbdea4`, each backend had its own `Drive drives[]` array and its own copy of the Reed-header open, geometry decode and offset math. That meant three near-identical copies of the file code *and* three separate sets of drives in what is supposed to be one machine — attaching a disk to "OMTI drive 0" and to "Xebec drive 0" produced two independent open files onto possibly different images.

Now `hard_slot[]` lives once in `trs_hard_image.c`, and `trs_hdctl.c` fans attach/remove out to every backend that can reach that unit. See [`../architecture/controller-abstraction.md`](../architecture/controller-abstraction.md).

Slot count is 4, the WD1000's cap, because its SDH drive field is two bits. The SASI controllers' 1-bit LUN reaches only units 0 and 1 — but that limit is enforced in each backend's own LUN decode, not in the slot layer, because that is where it lives on the hardware. The slot layer only exposes `hdctl_slot_wd1000_only()` so the GUI can label slots 2 and 3.

## Geometry comes from the image, never from the guest

**Decided:** `cyls`/`heads`/`secs` are read from the `.hdv` Reed header at open time and never changed. Every backend acknowledges its "set drive characteristics" command and ignores it for addressing — OMTI `SET CHARACTERISTICS` (`0x0C`), Xebec `INITIALIZE DRIVE CHARACTERISTICS` (`0x0C`).

This one was implemented the other way first, on the reasonable theory that real controller hardware is *programmed* for its drive exactly that way. It was reverted because **the real OMTI boot EPROM sends a stale, wrong characteristics block automatically at every startup**. Letting it override the live geometry corrupted every subsequent seek. A real OMTI 5527 wired for N heads answers CDB head values 0..N-1 regardless of what a boot ROM claims; the field is more plausibly write-precompensation or step-rate configuration than addressable-head count.

The declared geometry is still worth watching, so `XEBECDEBUG2` prints it next to the image's — a guest declaring a bigger drive than the image is exactly what walks a formatter off the end of the file.

## Write protection is enforced by opening read-only

**Decided:** `hard_image_open()` opens a write-protected image with `"rb"` instead of `"rb+"`, so writes fail at `fwrite()` in every backend.

The obvious design is an `if (d->writeprot) return error;` at the top of each write path. That is what the Xebec's `xebec_format()` does, and it is fine as far as it goes — but the OMTI's write paths never checked the flag at all (`77c95c8`), and neither did the Xebec's `WRITE`. Three backends each having to remember is three chances to forget.

Enforcing it one layer down means a protected image is protected whether or not the backend remembered. The image's own header flag (`flag1 & 0x80`) is honoured this way too, not just filesystem permissions — a `.hdv` marked protected internally stays protected on a writable filesystem.

## Xebec lives only at `0x00`–`0x02`

**Decided:** `0x40`–`0x43` is OMTI-exclusive. The Xebec is reached only through the TCS onboard SASI adapter.

The first Xebec implementation exposed the controller core behind *two* host-adapter interfaces: the TCS adapter at `0x00`–`0x02` and Holte's slot at `0x40`–`0x42`, with `trs_io.c` routing `0x40`–`0x42` dynamically to whichever of OMTI or Xebec had an image attached. The reasoning was that a Xebec card could physically sit in Holte's slot.

`b2a8c5a` removed it. Nothing drives a Xebec at `0x40`, the dynamic routing was the only place in the I/O dispatch that needed to arbitrate between two backends for one port, and the two sector sizes (256 on the TCS side, 512 on Holte's) made the "one core, two adapters" story more complicated than the thing it was modelling. The port ranges are now simply disjoint and statically routed.

## The WD1000 is relocated in `trs_io.c`, not in the backend

**Decided:** the Genie IIIs' `0x50`–`0x57` register file is mapped onto the native `0xC8`–`0xCF` by adding `0x78` at the dispatch site.

`trs_hard.c` is upstream xtrs code and only ever sees native addresses. Teaching it a second, machine-dependent port map would have meant carrying a local modification through every future upstream merge, for no behavioural gain. The Model 4P variant already used the same `+ 0x80` trick, so this follows an existing pattern rather than inventing one.

## Where the Xebec manual and real software disagree, real software wins

**Decided:** two documented values are deliberately not followed.

| Manual | Code | Evidence |
|---|---|---|
| `0x10` = READ SECTOR BUFFER | `0x0E` = READ SECTOR BUFFER (`0x10` accepted as a synonym) | `0x0E` is what G-DOS 2.4's driver issues and what the verified-working path uses |
| Format fill `0x6C` | `0xE5` | What the working `HDFORMAT` path has always seen; also CP/M's erased-directory byte |

Both are marked as unresolved in `trs_xebec.h`, not settled. Either would be reopened by an `XEBECDEBUG2` trace of a real G-DOS session showing otherwise. The principle is the general one for this project: the sources are incomplete and sometimes contradictory, so the tiebreak is what makes real 1980s software work. See [`recovered-source.md`](recovered-source.md).

## `READ VERIFY` is deliberately not range-checked

**Decided:** Xebec `READ VERIFY` (`0x09`) does not bounds-check its block address, even though `CHECK TRACK` right next to it does.

`READ` and `WRITE` do not range-check either. Making `READ VERIFY` the one command that starts rejecting addresses risks breaking a working G-DOS 2.4 path for the sake of consistency. If the range check goes in, it goes in for all three at once, behind a trace showing what G-DOS actually sends.

## What is not modelled at all

Stated once so it is not rediscovered as a bug:

- **No timing.** Step rates in WD1000 command bytes are parsed and discarded; every command completes instantly.
- **No DMA or interrupts.** The OMTI's `0x43` mask register is stored and otherwise ignored; the WD1000's `INTRQ` bit is not emulated.
- **No ECC.** The Xebec's `READ ECC BURST LENGTH` always returns zero.
- **No multi-sector on the WD1000.** A set `MULTI` flag is rejected with `ABRTERR`. The two SASI backends *do* support multi-sector via the DCB block count.
- **No ID fields.** A flat `.hdv` has nowhere to put them, so `CHECK TRACK` passes any in-range track and `FORMAT BAD TRACK` is identical to `FORMAT TRACK`.
