# zbx-dap

VS Code Debug Adapter Protocol (DAP) server that drives sdl2trs's built-in
`zbx` console debugger. Talks DAP to VS Code, plain-text zbx commands to
sdl2trs over a pty. No debug logic of its own -- zbx does the work.

## Install

```
tools/zbx-dap/install-extension.sh
```

Copies the local `zbx` debug type into `~/.vscode(-insiders)/extensions`.
Re-run after editing `adapter.py` or `extension/`, then reload the window.

## Configure

`.vscode/launch.json` entries of type `zbx` (see the two already there):
`sdl2trs` (binary path), `rom` (boot ROM image, required), `floppy`/`hard0`
(optional disk images), `omtisecsize` (OMTI hard disks), `stopOnEntry`.

## Features

- Launch, stop-on-entry, step (`n`), Continue. No step-over -- zbx has none,
  so a CALL always steps into the callee.
- Registers, in the Variables view under "Registers".
- Read Memory (`readMemory`, backed by zbx `peek`).
- Disassembly View: live at PC, instruction bytes + mnemonic, labeled with
  names from `gdos-2.4-addresses.md` where one is known.
- Instruction breakpoints: click the gutter in the Disassembly View. Real
  `b <addr>` in zbx, reported back verified.
- Source-line breakpoints on labeled lines (subroutine entry points): the
  file is assembled with pasmo and the label at that line resolved to a
  real address. Unlabeled lines fall back to the Disassembly View.
- Debug Console (`evaluate`): type any zbx command directly, e.g. `dump`,
  `peek 4200,4210`, or `b DOSERR` -- symbol names are resolved to hex first.
  Hover/watch never touch zbx, since a zbx command isn't side-effect-free.
- `cmdinfo.py`: standalone parser for a TRS-80 `/CMD` file's load blocks
  and entry address, for setting a breakpoint before running a program.

<!-- screenshot: Disassembly View with a live breakpoint and labeled addresses -->
<!-- screenshot: Debug Console evaluating a zbx command -->

## Known limits

- **Source-line breakpoints need a label.** pasmo has no per-line listing,
  only a label->address table, so an unlabeled instruction line has no
  address to give zbx. Set it on the Disassembly View instead.
- **Continue is one-way.** zbx's REPL blocks on `g` with no async break-in
  (no SIGINT handling). The only way back to stopped is a breakpoint. Don't
  Continue past code that spin-waits on real hardware (e.g. the boot ROM's
  port-0xF9 poll) -- it hangs forever under emulation with no way to pause.
  If you land there: stop the debug session (Disconnect), don't quit VS
  Code -- Disconnect kills the sdl2trs child cleanly.
- **`peek`/`readMemory` bypasses address mapping.** It reads raw RAM, not
  through the ROM-overlay/video/I/O window mapping that `mem_read()` (and
  disassemble) use. Fine for plain RAM, wrong for ROM-mapped addresses.
- Can't disassemble backwards (scrolling up past the reference address
  returns `??`/invalid) -- Z80 opcodes are variable-length, so there's no
  safe way to find a prior instruction boundary without decoding forward
  from an earlier, unknown-safe point.

## Debugging the adapter

Logs to `/tmp/zbx-dap.log` by default; set `ZBX_DAP_LOG` to override or
unset it to disable.

---
Copyright (c) 2026 Egbert H. Schroeer. BSD 2-Clause; see [LICENSE](../../LICENSE).
