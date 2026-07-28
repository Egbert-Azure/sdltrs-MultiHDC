<!-- /docs/README.md -->

# sdltrs-MultiHDC documentation

Four kinds of document, kept apart on purpose:

| Directory | Answers |
|---|---|
| [`guides/`](guides/) | How do I use it? |
| [`architecture/`](architecture/) | How does the emulator work today? |
| [`archaeology/`](archaeology/) | How did we find out, and why is it like this? |
| [`reference/`](reference/) | What did the original documents say? |

The split matters most between **architecture** and **archaeology**. Architecture describes the code as it stands and should be corrected when the code changes. Archaeology is a record of investigation and decision, including approaches that were tried and abandoned — it is not kept in sync with the code, and superseded material stays in it, marked as superseded.

## Start here

New to the project: [`guides/HOWTO.md`](guides/HOWTO.md), then [`architecture/controller-abstraction.md`](architecture/controller-abstraction.md).

Debugging a hard-disk problem: find the controller in `architecture/`, then check [`archaeology/design-decisions.md`](archaeology/design-decisions.md) before concluding something is a bug.

## guides/

| Document | Covers |
|---|---|
| [HOWTO.md](guides/HOWTO.md) | Building, attaching a disk, both verified boot scenarios, the Alt-H GUI, drive-count limits, expected quirks, debug flags |
| [changes-vs-upstream-sdltrs.md](guides/changes-vs-upstream-sdltrs.md) | File-by-file record of how this fork diverges from stock SDL2TRS: what was added, modified, and left alone |

## architecture/

| Document | Covers |
|---|---|
| [controller-abstraction.md](architecture/controller-abstraction.md) | **Read first.** Three backends, one slot table, one image layer, and port-range dispatch with no arbitration |
| [hdctl.md](architecture/hdctl.md) | `trs_hdctl.c` — the slot facade the GUI, config and CLI talk to |
| [omti.md](architecture/omti.md) | OMTI 5527 (`trs_omti.c`), ports `0x40`–`0x43`. Holte's CP/M 3.0 drives this one |
| [xebec.md](architecture/xebec.md) | Xebec S1410 (`trs_xebec.c`), ports `0x00`–`0x02`. G-DOS 2.4 drives this one |
| [wd1000.md](architecture/wd1000.md) | WD1000/1010 (`trs_hard.c`), ports `0x50`–`0x57`. Inherited from upstream, the only backend reaching all four slots |

## archaeology/

| Document | Covers |
|---|---|
| [design-decisions.md](archaeology/design-decisions.md) | What was chosen, what was rejected, and what settled it — including the decisions made twice |
| [controller-history.md](archaeology/controller-history.md) | The narrative: why a third controller was needed, the drives-5/6 mystery, and the port-trace that broke it open |
| [original-rom-behavior.md](archaeology/original-rom-behavior.md) | The two boot EPROMs, what each one can reach, and why the ROM-to-controller pairing is not 1:1 |
| [recovered-source.md](archaeology/recovered-source.md) | Provenance: which protocol fact came from which source, and how much weight each source carries |

## reference/

Primary documents, reproduced or scanned. Nothing here was written for this project.

| Document | |
|---|---|
| [Xebec S1410A Owner Manual_text.pdf](reference/Xebec%20S1410A%20Owner%20Manual_text.pdf) | The only genuine specification behind any of the three controllers |
| [Xebec SASI:MFM S1510A Signal DEf.md](reference/Xebec%20SASI%3AMFM%20S1510A%20Signal%20DEf.md) | SASI bus signal definitions — REQ, BSY, C/D, I/O, MSG |
| [G-DOS 2-4.md](reference/G-DOS%202-4.md) | G-DOS 2.4 overview and its cross-Genie model detection |
| [gdos-screen-formats.md](reference/gdos-screen-formats.md) | G-DOS 2.4 screen formats |
| [gdos_auszug-aus-manual.pdf](reference/gdos_auszug-aus-manual.pdf) | Scanned G-DOS manual excerpt |
| [Installation des Holte CPM-Plus.md](reference/Installation%20des%20Holte%20CPM-Plus.md) | Installing Holte's CP/M Plus on a real Genie IIIs (German, 1993) — also the account of how the Holte sources were obtained |
| [geniecpm.pdf](reference/geniecpm.pdf) | Scanned Genie CP/M manual excerpt |

The disk images, EPROM dumps and Holte sources themselves are not in this repository — see the companion archives linked from [`archaeology/recovered-source.md`](archaeology/recovered-source.md).
