#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""OPX fork: some functions' code from the pi32v2 objdump listing (build/felucca.dis), for reading the compiled
render loops without the toolchain. CI publishes it as dev/disasm.txt.
  tools/dis_funcs.py DIS NAME... > out.txt"""
import re
import sys

LABEL = re.compile(r"^([A-Za-z_][A-Za-z_0-9.]*):$")
want = set(sys.argv[2:])
cur = None
with open(sys.argv[1], errors="replace") as f:
    for line in f:
        m = LABEL.match(line.strip())
        if m and not m.group(1).startswith("."):
            cur = m.group(1) if m.group(1) in want else None
            if cur:
                print(f"\n==== {cur}")
            continue
        if cur:
            print(line.rstrip())
