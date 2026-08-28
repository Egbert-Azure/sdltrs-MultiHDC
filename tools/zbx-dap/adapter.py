#!/usr/bin/env python3
# /tools/zbx-dap/adapter.py
#
# A Debug Adapter Protocol (DAP) server that drives sdl2trs's built-in zbx
# console instead of implementing its own debugger. VS Code talks DAP to
# this process over stdin/stdout; this process talks zbx's plain-text
# console to sdl2trs over a pty, the same way run-stock-session.sh already
# does by hand.
#
# Scope (step 1 of the plan in the dev-environment notes): launch, single
# instruction stepping, continue, and a live register view. No breakpoints
# yet -- zbx only takes bare hex addresses, and mapping a source line to one
# needs either the annotated-disassembly address text or a pasmo listing
# correlation, neither of which is wired up here. setBreakpoints is answered
# but every breakpoint comes back unverified.
#
# zbx has no step-over: `n`/`s` both execute exactly one instruction, so a
# CALL always steps into the callee. DAP's "step over" therefore behaves
# identically to "step into" here.
#
# zbx also has no asynchronous break-in (no SIGINT handler, no keystroke
# read while the Z80 is running), so once `continue` is sent, the only way
# back to a stopped state is a breakpoint or a HALT configured to trap --
# neither exists yet in this build, so Continue is a one-way trip until you
# stop the session. That mirrors real zbx, not a bug here.
#
# readMemory (cmd_readMemory) is backed by zbx's `peek`, which calls
# mem_peek() in trs_memory.c -- a side-effect-free read straight out of the
# flat `memory[]` array, deliberately bypassing the address-mapping that
# mem_read() (what the Z80 actually sees, and what disassemble/`d` uses)
# applies for ROM overlay, video RAM, and keyboard-matrix I/O windows. For
# plain RAM the two agree, but for a ROM-mapped address -- which is most of
# what you're looking at when debugging "boot ROM only" -- peek reads
# whatever garbage sits underneath the ROM in the RAM array, not the ROM
# byte the CPU is executing. That's a zbx/emulator-level limitation (there's
# no other console command that reads memory without side effects), not a
# bug in this adapter; the disassemble view is unaffected since it goes
# through mem_read.
import base64
import json
import os
import pty
import re
import subprocess
import sys
import tempfile
import threading
import time

PROMPT = b"(zbx) "
LOG = os.environ.get("ZBX_DAP_LOG") or "/tmp/zbx-dap.log"

# zbx's main loop (debug.c) does `putchar('\n'); disassemble(Z80_PC);`
# immediately before printing every "(zbx) " prompt -- so the text preceding
# *every* prompt ends with a preview of the current instruction, which is
# not part of whatever command was actually run. Left in, it silently
# duplicates an address-prefixed line at the end of any response, which
# corrupts address-keyed parsing (parse_peek, parse_dis) and instruction
# chaining (Adapter._dis_lines). Strip it once, here, for every caller.
_TRAILING_PC_ECHO_RE = re.compile(rb"(?:\r\n|\n){2}[0-9a-fA-F]{4}:[^\r\n]*(?:\r\n|\n)?$")


def log(msg):
    if LOG:
        with open(LOG, "a") as f:
            f.write(f"{time.time():.3f} {msg}\n")


# --- DAP wire protocol -----------------------------------------------------

class Dap:
    def __init__(self):
        self._seq = 0
        self._out_lock = threading.Lock()
        self._stdin = sys.stdin.buffer
        self._stdout = sys.stdout.buffer

    def read_message(self):
        headers = {}
        while True:
            line = self._stdin.readline()
            if not line:
                return None
            line = line.decode("utf-8").rstrip("\r\n")
            if line == "":
                break
            k, _, v = line.partition(":")
            headers[k.strip().lower()] = v.strip()
        length = int(headers.get("content-length", "0"))
        body = self._stdin.read(length)
        return json.loads(body.decode("utf-8"))

    def _send(self, obj):
        self._seq += 1
        obj["seq"] = self._seq
        data = json.dumps(obj).encode("utf-8")
        with self._out_lock:
            self._stdout.write(f"Content-Length: {len(data)}\r\n\r\n".encode("ascii"))
            self._stdout.write(data)
            self._stdout.flush()

    def send_response(self, request, success=True, body=None, message=None):
        obj = {
            "type": "response",
            "request_seq": request["seq"],
            "command": request["command"],
            "success": success,
        }
        if body is not None:
            obj["body"] = body
        if message is not None:
            obj["message"] = message
        log(f"<- {request['command']} success={success} body={body}")
        self._send(obj)

    def send_event(self, event, body=None):
        obj = {"type": "event", "event": event}
        if body is not None:
            obj["body"] = body
        self._send(obj)


# --- zbx console session ----------------------------------------------------

class ZbxSession:
    def __init__(self):
        self.master_fd = None
        self.proc = None
        self._buf = b""
        self._eof = False
        self._cv = threading.Condition()

    def launch(self, args):
        sdl2trs = args["sdl2trs"]
        rom = args["rom"]
        floppy = args.get("floppy") or ""
        hard0 = args.get("hard0") or ""
        omtisecsize = args.get("omtisecsize") or 0

        fd, cfg_path = tempfile.mkstemp(prefix="zbx-dap-", suffix=".t8c")
        with os.fdopen(fd, "w") as f:
            f.write(f"model=1\nromfile1={rom}\n")

        cmd = [
            sdl2trs, cfg_path,
            "-disk0", floppy, "-disk1", "", "-disk2", "", "-disk3", "",
            "-hard0", hard0, "-hard1", "", "-hard2", "", "-hard3", "",
        ]
        if hard0 and omtisecsize:
            cmd += ["-omtisecsize", str(omtisecsize)]
        cmd += ["-nofullscreen", "-zbx"]

        log(f"spawning: {cmd}")
        master_fd, slave_fd = pty.openpty()
        self.proc = subprocess.Popen(
            cmd, stdin=slave_fd, stdout=slave_fd, stderr=slave_fd, close_fds=True,
        )
        os.close(slave_fd)
        self.master_fd = master_fd
        threading.Thread(target=self._reader, daemon=True).start()
        # Entering the debugger halts at 0000h before printing the first
        # prompt -- this is the natural "stopped at entry" state.
        return self.wait_for_prompt(timeout=15)

    def _reader(self):
        while True:
            try:
                chunk = os.read(self.master_fd, 4096)
            except OSError:
                chunk = b""
            with self._cv:
                if chunk:
                    self._buf += chunk
                else:
                    self._eof = True
                self._cv.notify_all()
            if not chunk:
                return

    def wait_for_prompt(self, timeout):
        deadline = None if timeout is None else time.time() + timeout
        with self._cv:
            while PROMPT not in self._buf:
                if self._eof:
                    raise EOFError("sdl2trs exited")
                remaining = None if deadline is None else deadline - time.time()
                if remaining is not None and remaining <= 0:
                    raise TimeoutError(f"no (zbx) prompt within timeout; buffer={self._buf!r}")
                self._cv.wait(timeout=remaining)
            idx = self._buf.index(PROMPT)
            text = self._buf[:idx]
            self._buf = self._buf[idx + len(PROMPT):]
            text = _TRAILING_PC_ECHO_RE.sub(b"", text)
            return text.decode("latin-1", "replace")

    def send_and_wait(self, command, timeout=8):
        os.write(self.master_fd, (command + "\n").encode())
        return self.wait_for_prompt(timeout=timeout)

    def send_nowait(self, command):
        os.write(self.master_fd, (command + "\n").encode())

    def terminate(self):
        if self.proc and self.proc.poll() is None:
            try:
                self.proc.terminate()
                self.proc.wait(timeout=3)
            except Exception as e:
                log(f"terminate: graceful shutdown failed ({e!r}), killing")
                try:
                    self.proc.kill()
                    self.proc.wait(timeout=3)
                except Exception as e2:
                    log(f"terminate: kill also failed: {e2!r}")


# --- zbx text parsing --------------------------------------------------------

_PAIR_RE = {
    "A": r"A F:\s*([0-9a-fA-F]{2})\s+([0-9a-fA-F]{2})",  # -> A, F
    "B": r"B C:\s*([0-9a-fA-F]{2})\s+([0-9a-fA-F]{2})",  # -> B, C
    "D": r"D E:\s*([0-9a-fA-F]{2})\s+([0-9a-fA-F]{2})",  # -> D, E
    "H": r"H L:\s*([0-9a-fA-F]{2})\s+([0-9a-fA-F]{2})",  # -> H, L
    "I": r"I R:\s*([0-9a-fA-F]{2})\s+([0-9a-fA-F]{2})",  # -> I, R
}
_PAIR_NAMES = {"A": ("A", "F"), "B": ("B", "C"), "D": ("D", "E"),
               "H": ("H", "L"), "I": ("I", "R")}
_WORD_RE = {name: rf"\b{name}:\s*([0-9a-fA-F]+)" for name in ("IX", "IY", "PC", "SP")}
_SHADOW_RE = {name: rf"{name}':\s*([0-9a-fA-F]+)" for name in ("AF", "BC", "DE", "HL")}


def parse_dump(text):
    regs = {}
    for key, pattern in _PAIR_RE.items():
        m = re.search(pattern, text)
        if m:
            n1, n2 = _PAIR_NAMES[key]
            regs[n1], regs[n2] = m.group(1), m.group(2)
    for name, pattern in _WORD_RE.items():
        m = re.search(pattern, text)
        if m:
            regs[name] = m.group(1)
    for name, pattern in _SHADOW_RE.items():
        m = re.search(pattern, text)
        if m:
            regs[name + "'"] = m.group(1)
    return regs


def parse_addr(ref):
    """DAP memory references are hex, optionally "0x"-prefixed."""
    ref = ref.strip()
    if ref[:2].lower() == "0x":
        ref = ref[2:]
    return int(ref, 16) & 0xFFFF


_SYMBOL_ROW_RE = re.compile(
    r"^\|\s*`([0-9A-Fa-f]{4})`\s*\|\s*`([A-Za-z_][A-Za-z0-9_]*)`\s*\|")
_SYMBOL_FILE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "gdos-2.4-addresses.md")


def load_symbols(path=_SYMBOL_FILE):
    """Reads the `| \`addr\` | \`NAME\` | ... |` table this project maintains
    by hand (see the file itself) into {address: name}. Missing file (a
    workspace with no such table yet) is not an error -- just no symbols."""
    symbols = {}
    try:
        with open(path) as f:
            for line in f:
                m = _SYMBOL_ROW_RE.match(line)
                if m:
                    symbols[int(m.group(1), 16)] = m.group(2)
    except FileNotFoundError:
        pass
    return symbols


_IDENT_RE = re.compile(r"[A-Za-z_][A-Za-z0-9_]*")

# pasmo has no per-line listing output, only a label->address symbol table
# (its 3rd positional argument). That only resolves a source-line breakpoint
# set on a line that *is* a label -- most instruction lines aren't -- but
# it's exactly the addresses worth breaking at (subroutine entry points),
# and needs no new machinery beyond pasmo itself.
_PASMO_SYM_RE = re.compile(r"^(\S+)\s+EQU\s+0?([0-9A-Fa-f]+)H\s*$", re.IGNORECASE)


def parse_pasmo_symbols(sym_path):
    """{name: address} from a pasmo symbol-table file."""
    symbols = {}
    with open(sym_path) as f:
        for line in f:
            m = _PASMO_SYM_RE.match(line.strip())
            if m:
                symbols[m.group(1)] = int(m.group(2), 16)
    return symbols


_LABEL_LINE_RE = re.compile(r"^([A-Za-z_][A-Za-z0-9_]*)\s*(\S*)")


def line_labels(asm_path):
    """{line_number (1-based): label} for lines that define a label at an
    address this file's own assembly generates -- i.e. NOT an EQU line,
    which names an address elsewhere (e.g. a ROM/DOS entry point) rather
    than emitting code at its own line."""
    labels = {}
    with open(asm_path) as f:
        for lineno, line in enumerate(f, start=1):
            if line[:1] in (" ", "\t", ";", "\n", ""):
                continue
            m = _LABEL_LINE_RE.match(line)
            if not m:
                continue
            name, next_tok = m.group(1), m.group(2)
            if next_tok.upper() == "EQU":
                continue
            labels[lineno] = name
    return labels


# print_memory() in debug.c prints, per row of up to 16 bytes:
#   "%.4x: " (6 chars) + 16 * "%.2x " (3 chars each, space-padded past the
#   last real byte) + "    " (4 chars) + one raw ASCII char per byte.
# The hex region is therefore always exactly 16*3 = 48 columns wide starting
# at column 6, regardless of how many bytes that row actually holds.
def parse_peek(text):
    """Returns {address: byte_value} for every byte peek printed."""
    result = {}
    for line in text.splitlines():
        if len(line) < 6 or line[4:6] != ": ":
            continue
        try:
            addr = int(line[0:4], 16)
        except ValueError:
            continue
        hex_region = line[6:6 + 48]
        for i in range(16):
            tok = hex_region[i * 3:i * 3 + 2].strip()
            if tok:
                try:
                    result[(addr + i) & 0xFFFF] = int(tok, 16)
                except ValueError:
                    pass
    return result


# disassemble() in dis.c prints, per instruction:
#   "%04x: " (6 chars) + 4 * "%02x " (3 chars each, space-padded past the
#   last real byte) + " " (1 char) + the mnemonic (tab-separated columns).
# So the hex region is a fixed 4*3 = 12 columns at column 6, and the
# mnemonic always starts at column 19.
def parse_dis(text):
    """Returns a list of (address, "xx xx xx" instruction bytes, mnemonic)."""
    out = []
    for line in text.splitlines():
        if len(line) < 19 or line[4:6] != ": ":
            continue
        try:
            addr = int(line[0:4], 16)
        except ValueError:
            continue
        hex_region = line[6:18]
        tokens = [hex_region[i * 3:i * 3 + 2].strip() for i in range(4)]
        tokens = [t for t in tokens if t]
        if not tokens:
            continue
        mnem = re.sub(r"\s+", " ", line[19:]).strip()
        out.append((addr, " ".join(tokens), mnem))
    return out


# --- the adapter proper ------------------------------------------------------

class Adapter:
    THREAD_ID = 1
    FRAME_ID = 1
    REGISTERS_REF = 1

    def __init__(self):
        self.dap = Dap()
        self.zbx = ZbxSession()
        self.running = False
        self.stop_on_entry = True
        # address -> zbx trap-table index, for breakpoints *this adapter*
        # armed via setInstructionBreakpoints (see cmd_setInstructionBreakpoints).
        self.instr_bp_indices = {}
        # source path -> {line: zbx trap-table index}, for breakpoints armed
        # via setBreakpoints (see cmd_setBreakpoints). Kept separate from
        # instr_bp_indices since DAP resends each source file's *own* full
        # set independently -- clearing one must never touch the other.
        self.line_bp_indices = {}
        # address -> name, from gdos-2.4-addresses.md (see load_symbols).
        # Used both ways: cmd_disassemble annotates addresses with names,
        # cmd_evaluate substitutes names back to hex before zbx ever sees
        # them, since zbx itself only understands bare addresses.
        self.symbols = load_symbols()
        self.symbol_addrs = {name.upper(): addr for addr, name in self.symbols.items()}

    def _resolve_symbols(self, text):
        """Replaces whole-word symbol names (case-insensitive) with their
        hex address, so the Debug Console can take `b DOSERR` as well as
        `b 4409`. zbx commands (b, del, peek, d, ...) are short lowercase
        words that never collide with a table name."""
        def repl(m):
            addr = self.symbol_addrs.get(m.group(0).upper())
            return f"{addr:x}" if addr is not None else m.group(0)
        return _IDENT_RE.sub(repl, text)

    def run(self):
        while True:
            req = self.dap.read_message()
            if req is None:
                return
            if req.get("type") != "request":
                continue
            handler = getattr(self, f"cmd_{req['command']}", None)
            log(f"-> {req['command']} {req.get('arguments')}")
            try:
                if handler:
                    handler(req)
                else:
                    self.dap.send_response(req, success=False, message="unsupported request")
            except Exception as e:
                log(f"!! {req['command']} raised {e!r}")
                self.dap.send_response(req, success=False, message=str(e))

    # -- lifecycle --

    def cmd_initialize(self, req):
        self.dap.send_response(req, body={
            "supportsConfigurationDoneRequest": True,
            "supportsReadMemoryRequest": True,
            "supportsDisassembleRequest": True,
            "supportsInstructionBreakpoints": True,
        })
        self.dap.send_event("initialized")

    def cmd_launch(self, req):
        args = req["arguments"]
        self.stop_on_entry = args.get("stopOnEntry", True)
        banner = self.zbx.launch(args)
        log(f"launch banner: {banner!r}")
        self.running = False
        self.dap.send_response(req)

    def cmd_configurationDone(self, req):
        self.dap.send_response(req)
        if self.stop_on_entry:
            self.dap.send_event("stopped", {
                "reason": "entry", "threadId": self.THREAD_ID, "allThreadsStopped": True,
            })

    def cmd_disconnect(self, req):
        # Acknowledge first: sdl2trs teardown can take a moment (SIGTERM,
        # then a wait, then SIGKILL if it didn't take), and there's no
        # reason to make VS Code wait on that before confirming disconnect.
        self.dap.send_response(req)
        self.zbx.terminate()
        sys.exit(0)

    def cmd_terminate(self, req):
        self.zbx.terminate()
        self.dap.send_response(req)

    # -- breakpoints --

    def _assemble_symbols(self, asm_path):
        """pasmo's label->address table for asm_path, or None if it won't
        assemble (e.g. mid-edit syntax error) -- distinct from "assembled
        fine but this particular label isn't in it"."""
        with tempfile.TemporaryDirectory() as td:
            obj = os.path.join(td, "out.bin")
            sym = os.path.join(td, "out.sym")
            try:
                subprocess.run(["pasmo", asm_path, obj, sym],
                                capture_output=True, text=True, timeout=15,
                                cwd=os.path.dirname(asm_path) or ".", check=True)
            except (subprocess.CalledProcessError, FileNotFoundError, subprocess.TimeoutExpired) as e:
                log(f"pasmo failed for {asm_path}: {e}")
                return None
            return parse_pasmo_symbols(sym)

    # Source-line breakpoints only resolve on lines that are themselves a
    # label -- pasmo has no per-line listing, only a label->address table
    # (see _assemble_symbols / line_labels), so a line with no label at its
    # start has no address to give zbx. That covers subroutine entry points,
    # which is most of what's worth breaking at; anything else still works
    # from the Disassembly View via setInstructionBreakpoints.
    def cmd_setBreakpoints(self, req):
        args = req["arguments"]
        path = args["source"]["path"]
        src_bps = args.get("breakpoints", [])

        for idx in self.line_bp_indices.pop(path, {}).values():
            self.zbx.send_and_wait(f"del {idx}")

        labels = line_labels(path)
        symbols = self._assemble_symbols(path)

        out = []
        new_indices = {}
        for bp in src_bps:
            line = bp["line"]
            name = labels.get(line)
            addr = symbols.get(name) if (symbols is not None and name) else None
            if addr is None:
                if symbols is None:
                    message = "pasmo could not assemble this file"
                elif name is None:
                    message = ("this line has no label -- set the breakpoint "
                               "on a labeled line, or from the Disassembly View instead")
                else:
                    message = f"'{name}' is not in pasmo's symbol table"
                out.append({"verified": False, "line": line, "message": message})
                continue
            text = self.zbx.send_and_wait(f"b {addr:x}")
            m = re.search(r"\[(\d+)\] at ([0-9a-fA-F]{4})", text)
            if m:
                idx, resolved = int(m.group(1)), int(m.group(2), 16)
                new_indices[line] = idx
                out.append({"verified": True, "line": line, "instructionReference": f"0x{resolved:04x}"})
            else:
                out.append({
                    "verified": False, "line": line,
                    "message": text.strip() or "zbx did not confirm the breakpoint",
                })

        self.line_bp_indices[path] = new_indices
        self.dap.send_response(req, body={"breakpoints": out})

    def cmd_setExceptionBreakpoints(self, req):
        self.dap.send_response(req, body={})

    def cmd_setInstructionBreakpoints(self, req):
        # zbx keeps one flat trap table with no notion of "this adapter's
        # breakpoints" vs. anyone else's, and DAP resends the *full* desired
        # set on every call -- so the simplest correct sync is: clear every
        # trap we previously armed, then arm exactly what's being asked for
        # now, using the fresh trap-table indices `b` reports back.
        for idx in self.instr_bp_indices.values():
            self.zbx.send_and_wait(f"del {idx}")
        self.instr_bp_indices = {}

        out = []
        for bp in req["arguments"].get("breakpoints", []):
            addr = (parse_addr(bp["instructionReference"]) + int(bp.get("offset") or 0)) & 0xFFFF
            text = self.zbx.send_and_wait(f"b {addr:x}")
            m = re.search(r"\[(\d+)\] at ([0-9a-fA-F]{4})", text)
            if m:
                idx, resolved = int(m.group(1)), int(m.group(2), 16)
                self.instr_bp_indices[resolved] = idx
                out.append({"verified": True, "instructionReference": f"0x{resolved:04x}"})
            else:
                out.append({
                    "verified": False, "instructionReference": f"0x{addr:04x}",
                    "message": text.strip() or "zbx did not confirm the breakpoint",
                })
        self.dap.send_response(req, body={"breakpoints": out})

    # -- execution control --

    def _stop(self, reason):
        self.running = False
        self.dap.send_event("stopped", {
            "reason": reason, "threadId": self.THREAD_ID, "allThreadsStopped": True,
        })

    def cmd_continue(self, req):
        self.running = True
        self.zbx.send_nowait("g")
        self.dap.send_response(req, body={"allThreadsContinued": True})

        def watch():
            try:
                text = self.zbx.wait_for_prompt(timeout=None)
                reason = "breakpoint" if "Stopped at" in text else "pause"
                self._stop(reason)
            except EOFError:
                self.running = False
                self.dap.send_event("terminated")

        threading.Thread(target=watch, daemon=True).start()

    def cmd_pause(self, req):
        self.dap.send_response(
            req, success=False,
            message="zbx has no asynchronous break-in; Continue can't be interrupted",
        )

    def cmd_next(self, req):
        self._step(req, "n")

    def cmd_stepIn(self, req):
        self._step(req, "n")

    def _step(self, req, zbx_cmd):
        if self.running:
            self.dap.send_response(req, success=False, message="target is running")
            return
        self.zbx.send_and_wait(zbx_cmd)
        self.dap.send_response(req)
        self._stop("step")

    # -- state inspection --

    def cmd_threads(self, req):
        self.dap.send_response(req, body={"threads": [{"id": self.THREAD_ID, "name": "Z80"}]})

    def cmd_stackTrace(self, req):
        dump = self.zbx.send_and_wait("dump")
        regs = parse_dump(dump)
        pc = regs.get("PC", "0000")
        frame = {
            "id": self.FRAME_ID,
            "name": f"{pc.upper()}h",
            "line": 0,
            "column": 0,
            "instructionPointerReference": f"0x{pc}",
        }
        self.dap.send_response(req, body={"stackFrames": [frame], "totalFrames": 1})

    def cmd_scopes(self, req):
        self.dap.send_response(req, body={"scopes": [
            {"name": "Registers", "variablesReference": self.REGISTERS_REF, "expensive": False},
        ]})

    def cmd_variables(self, req):
        if req["arguments"]["variablesReference"] != self.REGISTERS_REF:
            self.dap.send_response(req, body={"variables": []})
            return
        dump = self.zbx.send_and_wait("dump")
        regs = parse_dump(dump)
        order = ["PC", "SP", "A", "F", "B", "C", "D", "E", "H", "L",
                 "IX", "IY", "I", "R", "AF'", "BC'", "DE'", "HL'"]
        # Registers that hold a 16-bit memory address get a memoryReference
        # so VS Code's "View Memory" context-menu action works on them.
        pointer_regs = {"PC", "SP", "HL", "IX", "IY"}
        variables = []
        for n in order:
            if n not in regs:
                continue
            v = {"name": n, "value": regs[n], "variablesReference": 0}
            if n in pointer_regs:
                v["memoryReference"] = f"0x{regs[n]}"
            variables.append(v)
        self.dap.send_response(req, body={"variables": variables})

    # VS Code fires evaluate uninvited for hovers and watch expressions, not
    # just Debug Console input -- and a zbx command is not a side-effect-free
    # expression (`g` continues, `n`/`s` step, `del N` drops a breakpoint).
    # Only the Debug Console (context "repl") gets to drive zbx directly;
    # other contexts get an empty result instead of accidentally running
    # whatever text VS Code hovered over as a zbx command. `g` will still
    # block here until zbx's own timeout -- use Continue for that instead.
    def cmd_evaluate(self, req):
        args = req["arguments"]
        if args.get("context") not in (None, "repl"):
            self.dap.send_response(req, body={"result": "", "variablesReference": 0})
            return
        expr = self._resolve_symbols(args["expression"])
        text = self.zbx.send_and_wait(expr)
        self.dap.send_response(req, body={"result": text.rstrip("\r\n"), "variablesReference": 0})

    # -- memory / disassembly --

    def cmd_readMemory(self, req):
        args = req["arguments"]
        addr = (parse_addr(args["memoryReference"]) + int(args.get("offset") or 0)) & 0xFFFF
        count = int(args["count"])
        if count <= 0:
            self.dap.send_response(req, body={"address": f"0x{addr:04x}", "data": ""})
            return

        end = min(addr + count - 1, 0xFFFF)
        count = end - addr + 1
        cmd = f"peek {addr:x}" if count == 1 else f"peek {addr:x},{end:x}"
        text = self.zbx.send_and_wait(cmd, timeout=20)
        byte_map = parse_peek(text)

        data = bytearray()
        a = addr
        for _ in range(count):
            if a not in byte_map:
                break
            data.append(byte_map[a])
            a = (a + 1) & 0xFFFF

        body = {"address": f"0x{addr:04x}", "data": base64.b64encode(bytes(data)).decode("ascii")}
        if len(data) < count:
            body["unreadableBytes"] = count - len(data)
        self.dap.send_response(req, body=body)

    def _dis_lines(self, start_addr, n):
        """Returns up to n consecutive (addr, hex_bytes, mnemonic) tuples
        starting at start_addr, chaining zbx's fixed-10-line `d` command."""
        out = []
        addr = start_addr & 0xFFFF
        guard = 0
        while len(out) < n and guard < n + 20:
            guard += 1
            text = self.zbx.send_and_wait(f"d {addr:x}", timeout=20)
            lines = parse_dis(text)
            if not lines:
                break
            for line in lines:
                if len(out) < n:
                    out.append(line)
            last_addr, last_hex, _ = lines[-1]
            addr = (last_addr + len(last_hex.split())) & 0xFFFF
        return out

    def cmd_disassemble(self, req):
        args = req["arguments"]
        base = (parse_addr(args["memoryReference"]) + int(args.get("offset") or 0)) & 0xFFFF
        instr_offset = int(args.get("instructionOffset") or 0)
        count = int(args["instructionCount"])

        instructions = []

        # zbx can only disassemble forward from an address, so instructions
        # before the reference point (a negative instructionOffset, which VS
        # Code uses to scroll up) can't be resolved -- Z80 opcodes are
        # variable-length, so there's no reliable way to find a prior
        # instruction boundary without decoding forward from *somewhere*
        # earlier and hoping it realigns. Report them as invalid rather than
        # guess.
        n_before = min(max(-instr_offset, 0), count)
        for i in range(n_before):
            a = (base + instr_offset + i) & 0xFFFF
            instructions.append({
                "address": f"0x{a:04x}", "instruction": "??",
                "presentationHint": "invalid",
            })

        remaining = count - n_before
        if remaining > 0:
            lines_to_skip = max(instr_offset, 0)
            lines = self._dis_lines(base, lines_to_skip + remaining)
            for addr, hexbytes, mnem in lines[lines_to_skip:]:
                instr = {
                    "address": f"0x{addr:04x}",
                    "instructionBytes": hexbytes,
                    "instruction": mnem,
                }
                symbol = self.symbols.get(addr)
                if symbol:
                    instr["symbol"] = symbol
                instructions.append(instr)

        # zbx always returns what it's asked for, but pad defensively so a
        # short read (e.g. session torn down mid-request) still returns
        # exactly `count` entries, as required by the DAP spec.
        while len(instructions) < count:
            last = int(instructions[-1]["address"], 16) if instructions else base
            a = (last + 1) & 0xFFFF
            instructions.append({
                "address": f"0x{a:04x}", "instruction": "??",
                "presentationHint": "invalid",
            })

        self.dap.send_response(req, body={"instructions": instructions})


def main():
    Adapter().run()


if __name__ == "__main__":
    main()
