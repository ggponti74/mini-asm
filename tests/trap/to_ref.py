#!/usr/bin/env python3
"""Turn a mini-asm source into something GNU as (m68k, -mri) accepts:
   drop ';' comments, drop the colon after a label, remove blanks after commas."""
import re, sys
out = []
for line in sys.stdin:
    line = line.rstrip("\n")
    res, inq = [], False
    for ch in line:
        if ch == '"':
            inq = not inq
        if ch == ';' and not inq:
            break
        res.append(ch)
    line = "".join(res).rstrip()
    m = re.match(r'^([A-Za-z_][A-Za-z0-9_]*):(.*)$', line)
    if m:
        line = m.group(1) + m.group(2)
    # blanks after commas outside quotes
    res, inq, i = [], False, 0
    while i < len(line):
        ch = line[i]
        if ch == '"':
            inq = not inq
        res.append(ch)
        if ch == ',' and not inq:
            while i + 1 < len(line) and line[i + 1] in ' \t':
                i += 1
        i += 1
    out.append("".join(res))
print("\n".join(out))