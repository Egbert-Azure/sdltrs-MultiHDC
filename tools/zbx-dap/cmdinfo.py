#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2026 Egbert H. Schroeer
#
# Parses a TRS-80 /CMD file's load blocks and prints its entry address.
#
# TRS-80 /CMD file parser, used alongside the zbx bridge. Contains no
# zbx or xtrs code. See LICENSE for the full BSD 2-Clause text.

"""Parse a TRS-80 /CMD file: list its load blocks and print its entry address.

A /CMD file is not a fixed-TPA image -- it's a sequence of tagged blocks.
The two that matter here:

  0x01 <len> <addr_lo> <addr_hi> <data...>   load block
      len counts the 2 address bytes plus the data. A single byte can't
      encode a block over 255 bytes, so len values 0/1/2 wrap to 256/257/258
      -- i.e. 254/255/256 data bytes. len 3..255 is taken at face value.

  0x02 <len> <addr_lo> <addr_hi>             transfer/entry address block
      marks where execution starts -- this is the address you want for
      a zbx breakpoint before running the program.

Everything else (0x05 module-name header, 0x06 PDS header, etc.) is
skipped by its own length byte rather than decoded, since none of it
carries a load address or the entry point.
"""
import argparse
import json
import sys

KNOWN_SKIP_BLOCKS = {
    0x03: "end-of-PDS-member",
    0x05: "load module header",
    0x06: "PDS header",
    0x07: "patch name header",
    0x08: "ISAM directory entry",
    0x0C: "end of ISAM directory",
}


def parse_cmd(path):
    """Return (blocks, entry_addr) where blocks is [(load_addr, length), ...]
    in file order and entry_addr is an int or None if no transfer block was found.
    """
    with open(path, "rb") as f:
        data = f.read()

    blocks = []
    entry_addr = None
    i = 0
    n = len(data)
    while i < n:
        block_type = data[i]
        i += 1
        if block_type == 0x00:
            break  # padding/EOF, not a defined block type

        if i >= n:
            raise ValueError(f"truncated file: block type 0x{block_type:02x} at offset {i-1} has no length byte")
        length = data[i]
        i += 1

        if block_type == 0x01:
            if length < 3:
                length += 256  # wraps to a 256/257/258-byte block (254/255/256 data bytes)
            if i + 2 > n:
                raise ValueError(f"truncated load block at offset {i-2}")
            load_addr = data[i] | (data[i + 1] << 8)
            data_len = length - 2
            if i + 2 + data_len > n:
                raise ValueError(f"load block at offset {i-2} claims {data_len} data bytes past EOF")
            blocks.append((load_addr, data_len))
            i += 2 + data_len
        elif block_type == 0x02:
            if i + 2 > n:
                raise ValueError(f"truncated transfer block at offset {i-2}")
            entry_addr = data[i] | (data[i + 1] << 8)
            i += length
        else:
            i += length  # unrecognized/uninteresting block, skip its payload

    return blocks, entry_addr


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("cmd_file")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    args = ap.parse_args()

    try:
        blocks, entry_addr = parse_cmd(args.cmd_file)
    except (OSError, ValueError) as e:
        print(f"error: {e}", file=sys.stderr)
        sys.exit(1)

    if args.json:
        print(json.dumps({
            "blocks": [{"address": a, "length": l} for a, l in blocks],
            "entry": entry_addr,
        }))
        return

    print(f"{args.cmd_file}")
    for addr, length in blocks:
        print(f"  load {addr:04X}-{addr + length - 1:04X}  ({length} bytes)")
    if entry_addr is not None:
        print(f"  entry {entry_addr:04X}")
    else:
        print("  entry: none found (no 0x02 transfer block)")


if __name__ == "__main__":
    main()
