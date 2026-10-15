#!/usr/bin/env python3
"""Memory shifts/rotates (word, by one) checked against the 68000 manual's rules instead of
qemu-m68k: QEMU 8.2's memory-shift code treats LSR (a0) as ASR and never sets V for ASL."""
import os, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import diff

def model(mn, v):
    w = (v >> 16) & 0xFFFF
    msb, lsb = (w >> 15) & 1, w & 1
    V = 0
    if mn in ("asl", "lsl"):
        r = (w << 1) & 0xFFFF; C = msb
        if mn == "asl": V = int(((r >> 15) & 1) != msb)
    elif mn == "asr":
        r = (w >> 1) | (w & 0x8000); C = lsb
    elif mn == "lsr":
        r = w >> 1; C = lsb
    elif mn == "rol":
        r = ((w << 1) | msb) & 0xFFFF; C = msb
    else:  # ror
        r = (w >> 1) | (lsb << 15); C = lsb
    N = (r >> 15) & 1; Z = int(r == 0)
    mask = N * 1 + Z * 2 + V * 4 + C * 8
    return struct.pack(">II", (v & 0xFFFF) | (r << 16), mask)

bad = 0
for mn in ("asl", "asr", "lsl", "lsr", "rol", "ror"):
    L = lambda v: ["lea     cell, a0", "move.l  #$%x, (a0)" % v]
    cases = [(L(v), ["%s.w  (a0)" % mn], "capm") for v in diff.VALUES]
    expect = b"".join(model(mn, v) for v in diff.VALUES)
    for t in diff.TARGETS:
        got, info = diff.run_target(diff.build(cases, "mini"), "ms_" + mn, t)
        ok = got == expect
        print("%s.w (a0) vs the manual's rules  %-4s %s" % (mn, t, "ok" if ok else "DIFFERS"))
        bad += not ok
# the same shifts addressed with a displacement: base register b points 4 bytes before / after / at
# the cell, so 4(b), -4(b) and 0(b) all reach it (b = a0, which is ESI on x86, and a3, which is spilled)
for b in ("a0", "a3"):
    for disp, adjust in ((4, "subq.l  #4, %s"), (-4, "addq.l  #4, %s"), (0, None)):
        for mn in ("asl", "asr", "lsl", "lsr", "rol", "ror"):
            setup = ["lea     cell, %s" % b] + ([adjust % b] if adjust else []) + ["lea     cell, a5"]
            cases = [(setup + ["move.l  #$%x, (a5)" % v], ["%s.w  %d(%s)" % (mn, disp, b)], "capx") for v in diff.VALUES]
            expect = b"".join(model(mn, v) for v in diff.VALUES)
            for t in diff.TARGETS:
                got, info = diff.run_target(diff.build(cases, "mini"), "msd_%s_%s_%d" % (mn, b, disp), t)
                ok = got == expect
                print("%s.w %d(%s) vs the manual's rules  %-4s %s" % (mn, disp, b, t, "ok" if ok else "DIFFERS"))
                bad += not ok

print("model check:", "all match" if not bad else "%d mismatches" % bad)
sys.exit(1 if bad else 0)
