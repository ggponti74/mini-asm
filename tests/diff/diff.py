#!/usr/bin/env python3
"""Differential test of 68K logic/shift/rotate instructions.

The same source is (a) assembled by mini-asm for elf / arm / x86(PE, under wine)
and (b) by GNU as for real m68k (qemu-m68k).  Each program runs a table of
cases; after every case a subroutine records the result register and the
N/Z/V/C flags (read through Bcc, which does not change flags; X can't be
read this way) into a buffer, and the buffer is written to stdout.
All outputs must be identical.
"""
import os, subprocess, sys, itertools, tempfile, struct

HERE = os.path.dirname(os.path.abspath(__file__))
MA = os.environ.get("MINI_ASM", os.path.join(HERE, "..", "..", "mini-asm"))
TARGETS = os.environ.get("TARGETS", "elf,arm,pe").split(",")
WORK = tempfile.mkdtemp(prefix="difft")
WINE_ENV = dict(os.environ, WINEDEBUG="-all")

VALUES = [0, 1, 0x80, 0x7F, 0xFF, 0x8000, 0x7FFF, 0xFFFF, 0x7FFFFFFF,
          0x80000000, 0xFFFFFFFF, 0x12345678, 0xA5C3E1F7]
REG_COUNTS = [0, 1, 2, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 100, 127, 128, 255, 0x1000001]

CAP = """
cap     bpl     cap_n
        addq.l  #1, a2
cap_n   bne     cap_z
        addq.l  #2, a2
cap_z   bvc     cap_v
        addq.l  #4, a2
cap_v   bcc     cap_c
        addq.l  #8, a2
cap_c   move.l  d1, (a1)+
        move.l  a2, d3
        move.l  d3, (a1)+
        move.l  #0, a2
        rts
capx    bpl     capx_n
        addq.l  #1, a2
capx_n  bne     capx_z
        addq.l  #2, a2
capx_z  bvc     capx_v
        addq.l  #4, a2
capx_v  bcc     capx_c
        addq.l  #8, a2
capx_c  move.l  (a5), d1
        move.l  d1, (a1)+
        move.l  a2, d3
        move.l  d3, (a1)+
        move.l  #0, a2
        rts
capm    bpl     capm_n
        addq.l  #1, a2
capm_n  bne     capm_z
        addq.l  #2, a2
capm_z  bvc     capm_v
        addq.l  #4, a2
capm_v  bcc     capm_c
        addq.l  #8, a2
capm_c  move.l  (a0), d1
        move.l  d1, (a1)+
        move.l  a2, d3
        move.l  d3, (a1)+
        move.l  #0, a2
        rts
"""

def build(cases, kind, arena=False):
    """cases: list of (setup lines, instruction lines).  kind: 'mini' or 'ref'."""
    n = len(cases)
    nbytes = 8 * n
    out = ["start:  lea     results, a1", "        move.l  #0, a2"]
    for c in cases:
        setup, instr = c[0], c[1]
        out += ["        " + s for s in setup]
        out += ["        " + s for s in instr]
        out.append("        bsr     " + (c[2] if len(c) > 2 else "cap"))
    out.append("        bra     done")
    out.append(CAP)
    if kind == "mini":
        out += ["done    lea     results, a0", "        move.l  #1, d1",
                "        move.l  #%d, d2" % nbytes, "        move.l  #2, d0", "        trap    #0",
                "        moveq   #0, d0", "        rts"]
    else:
        out += ["done    move.l  #4, d0", "        move.l  #1, d1", "        move.l  #results, d2",
                "        move.l  #%d, d3" % nbytes, "        trap    #0",
                "        moveq   #0, d1", "        move.l  #1, d0", "        trap    #0"]
    lines = (nbytes + 31) // 32
    out.append("results dc.l    0,0,0,0,0,0,0,0")
    out += ["        dc.l    0,0,0,0,0,0,0,0"] * (lines - 1)
    out.append("cell    dc.l    0")
    if arena:
        # 512 bytes with a pattern that differs at every address; cellmid is the middle
        pat = [(i * 7 + 3) & 0xFF for i in range(512)]
        for half, label in ((0, "arena   "), (256, "cellmid ")):
            for j in range(0, 256, 16):
                out.append((label if j == 0 else "        ") + "dc.b    " + ",".join(str(b) for b in pat[half + j:half + j + 16]))
    return "\n".join(out) + "\n"

def sh(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, **kw)

def run_ref(src, name):
    asm = os.path.join(WORK, name + ".asm"); s = os.path.join(WORK, name + ".s")
    o = os.path.join(WORK, name + ".o"); exe = os.path.join(WORK, name + ".ref")
    open(asm, "w").write(src)
    open(s, "w").write(subprocess.run([sys.executable, os.path.join(HERE, "to_ref.py")], input=src.encode(),
                                      capture_output=True).stdout.decode())
    r = sh(["m68k-linux-gnu-as", "-mri", "-m68000", "-o", o, s])
    if r.returncode: return None, "reference assembly failed: " + r.stderr.decode()[:300]
    sh(["m68k-linux-gnu-ld", "-N", "-o", exe, o])
    r = sh(["qemu-m68k", exe], timeout=60)
    return r.stdout, "reference exit %d" % r.returncode

def run_target(src, name, t):
    asm = os.path.join(WORK, name + "." + t + ".asm"); exe = os.path.join(WORK, name + "." + t)
    open(asm, "w").write(src)
    tt = {"elf": "elf", "arm": "arm", "pe": "x86"}[t]
    r = sh([MA, "-t", tt, "-o", exe, asm])
    if r.returncode:
        err = [l for l in (r.stdout + r.stderr).decode().splitlines() if l.startswith("Error")]
        return None, "assembly failed: " + (err[0] if err else "?")
    if t == "arm": cmd = ["qemu-arm", exe]
    elif t == "pe": cmd = ["wine", exe]
    else: cmd = [exe]
    r = sh(cmd, timeout=120, env=WINE_ENV if t == "pe" else None)
    return r.stdout, "exit %d" % r.returncode

def describe(case, ref8, got8):
    r1, rm = struct.unpack(">II", ref8); g1, gm = struct.unpack(">II", got8)
    fl = lambda m: "".join(c if m & b else "-" for c, b in zip("NZVC", (1, 2, 4, 8)))
    ins = " ; ".join(case[1])
    vals = " ".join(x.split("#")[1].split(",")[0] for x in case[0] if "#" in x)
    return "%-22s [%s]  ref %08x %s   got %08x %s" % (ins, vals, r1, fl(rm), g1, fl(gm))

ARENA = False

def check(name, cases, show=6):
    ref, rinfo = run_ref(build(cases, "ref", ARENA), name)
    if ref is None or len(ref) != 8 * len(cases):
        print("%-22s REFERENCE PROBLEM: %s (%s bytes)" % (name, rinfo, None if ref is None else len(ref))); return 0, len(TARGETS)
    bad_targets = 0
    for t in TARGETS:
        got, info = run_target(build(cases, "mini", ARENA), name, t)
        if got is None:
            print("%-22s %-4s %s" % (name, t, info)); bad_targets += 1; continue
        if got == ref:
            print("%-22s %-4s ok (%d cases)" % (name, t, len(cases))); continue
        bad_targets += 1
        if len(got) != len(ref):
            print("%-22s %-4s output length %d, expected %d (%s)" % (name, t, len(got), len(ref), info)); continue
        diffs = [i for i in range(len(cases)) if got[8*i:8*i+8] != ref[8*i:8*i+8]]
        print("%-22s %-4s %d of %d cases differ" % (name, t, len(diffs), len(cases)))
        for i in diffs[:show]:
            print("      " + describe(cases[i], ref[8*i:8*i+8], got[8*i:8*i+8]))
    return len(TARGETS) - bad_targets, len(TARGETS)

def shift_cases(mn, sz, form):
    cases = []
    for v in VALUES:
        if form == "imm":
            for c in range(1, 9):
                cases.append((["move.l  #$%x, d1" % v], ["%s.%s  #%d, d1" % (mn, sz, c)]))
        else:
            for c in REG_COUNTS:
                cases.append((["move.l  #$%x, d1" % v, "move.l  #$%x, d2" % c], ["%s.%s  d2, d1" % (mn, sz)]))
    return cases

def unary_cases(mn, sz):
    return [(["move.l  #$%x, d1" % v], ["%s.%s  d1" % (mn, sz)]) for v in VALUES]

def binary_cases(mn, sz):
    return [(["move.l  #$%x, d1" % a, "move.l  #$%x, d2" % b], ["%s.%s  d2, d1" % (mn, sz)])
            for a in VALUES for b in VALUES]

if __name__ == "__main__":
    which = sys.argv[1:] or ["unary", "binary", "shift", "mem", "imm", "misc", "disp"]
    tot = ok = 0
    groups = []
    if "unary" in which:
        for mn in ("not", "neg", "clr", "tst"):
            for sz in "bwl": groups.append(("%s.%s" % (mn, sz), unary_cases(mn, sz)))
    if "mem" in which:
        L = lambda v: ["lea     cell, a0", "move.l  #$%x, (a0)" % v]
        for mn in ("not", "neg", "clr", "tst"):
            for sz in "bwl":
                groups.append(("%s.%s (a0)" % (mn, sz), [(L(v), ["%s.%s  (a0)" % (mn, sz)], "capm") for v in VALUES]))
        for mn in ("and", "or", "eor"):
            for sz in "bwl":
                groups.append(("%s.%s d2,(a0)" % (mn, sz), [(L(a) + ["move.l  #$%x, d2" % b], ["%s.%s  d2, (a0)" % (mn, sz)], "capm") for a in VALUES for b in VALUES]))
        for mn in ("and", "or"):
            for sz in "bwl":
                groups.append(("%s.%s (a0),d1" % (mn, sz), [(L(a) + ["move.l  #$%x, d1" % b], ["%s.%s  (a0), d1" % (mn, sz)]) for a in VALUES for b in VALUES]))
        # (memory shifts/rotates are checked by memshift_model.py: qemu-m68k's memory-shift
        #  code treats LSR as ASR and never sets V for ASL, so it can't be the oracle there)
    if "imm" in which:
        IMMS = {"b": [0, 1, 0x7F, 0x80, 0xFF], "w": [0, 1, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF],
                "l": [0, 1, 0xFF, 0x7FFF, 0x8000, 0xFFFF, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x12345678]}
        L = lambda v: ["lea     cell, a0", "move.l  #$%x, (a0)" % v]
        for mn in ("andi", "ori", "eori", "and", "or", "eor", "addi", "subi"):
            for sz in "bwl":
                groups.append(("%s.%s #imm,d1" % (mn, sz), [(["move.l  #$%x, d1" % v], ["%s.%s  #$%x, d1" % (mn, sz, i)])
                                                            for v in VALUES for i in IMMS[sz]]))
        for mn in ("andi", "ori", "eori", "and", "or", "eor"):
            for sz in "bwl":
                groups.append(("%s.%s #imm,(a0)" % (mn, sz), [(L(v), ["%s.%s  #$%x, (a0)" % (mn, sz, i)], "capm")
                                                              for v in VALUES for i in IMMS[sz]]))
    if "disp" in which:
        ARENA = True
        DS = [0, 1, 2, 4, -1, -2, -4, 127, 128, -128, -129, 200, -200, 252]
        DS_W = [d for d in DS if d % 2 == 0]
        for b in ("a0", "a3"):
            setup = lambda d, b=b: ["lea     cellmid, %s" % b, "lea     %d(%s), a5" % (d, b)]
            # loads (d1 preset, so a size merge that clobbers bits shows) and LEA
            for sz in "bwl":
                ds = DS if sz == "b" else DS_W
                groups.append(("disp.%s.move.%s.load" % (b, sz), [(setup(d) + ["move.l  #$A5A5A5A5, d1"], ["move.%s  %d(%s), d1" % (sz, d, b)]) for d in ds]))
            groups.append(("disp.%s.lea" % b, [(setup(d), ["lea     %d(%s), a6" % (d, b), "move.l  (a6), d1"]) for d in DS]))
            groups.append(("disp.%s.movea.w" % b, [(setup(d), ["move.w  %d(%s), a6" % (d, b), "move.l  a6, d1"]) for d in DS_W]))
            groups.append(("disp.%s.movea.l" % b, [(setup(d), ["move.l  %d(%s), a6" % (d, b), "move.l  a6, d1"]) for d in DS_W]))
            # stores (register and immediate source) and the memory read-back through a5
            for sz in "bwl":
                ds = DS if sz == "b" else DS_W
                vals = [0, 0x80, 0x7FFF, 0x8000, 0xFFFFFFFF, 0x12345678]
                groups.append(("disp.%s.move.%s.store" % (b, sz), [(setup(d) + ["move.l  #$%x, d1" % v], ["move.%s  d1, %d(%s)" % (sz, d, b)], "capx") for d in ds for v in vals]))
                groups.append(("disp.%s.move.%s.storei" % (b, sz), [(setup(d), ["move.%s  #$5A, %d(%s)" % (sz, d, b)], "capx") for d in ds]))
            # arithmetic / logic / compare with d(An) as the source or the destination
            for mn in ("and", "or"):  # (ADD/SUB/CMP take no memory source in mini-asm, with or without a displacement)
                for sz in "bwl":
                    ds = [0, 1, 4, -4, 200] if sz == "b" else [0, 2, 4, -4, 200]
                    groups.append(("disp.%s.%s.%s.src" % (b, mn, sz), [(setup(d) + ["move.l  #$%x, d1" % v], ["%s.%s  %d(%s), d1" % (mn, sz, d, b)]) for d in ds for v in (0, 0x7F, 0x80, 0xFFFFFFFF, 0x12345678)]))
            for mn in ("and", "or", "eor"):
                for sz in "bwl":
                    ds = [0, 1, 4, -4, 200] if sz == "b" else [0, 2, 4, -4, 200]
                    groups.append(("disp.%s.%s.%s.dst" % (b, mn, sz), [(setup(d) + ["move.l  #$12345678, (a5)", "move.l  #$%x, d1" % v], ["%s.%s  d1, %d(%s)" % (mn, sz, d, b)], "capx") for d in ds for v in (0, 0xFF, 0x80, 0xFFFFFFFF)]))
            # one-operand instructions, quick arithmetic, bit operations, shifts
            for mn in ("clr", "tst", "not", "neg"):
                for sz in "bwl":
                    ds = [0, 1, 4, -4, 200] if sz == "b" else [0, 2, 4, -4, 200]
                    groups.append(("disp.%s.%s.%s" % (b, mn, sz), [(setup(d) + ["move.l  #$%x, (a5)" % v], ["%s.%s  %d(%s)" % (mn, sz, d, b)], "capx") for d in ds for v in (0, 0x80, 0x7FFFFFFF, 0xFFFFFFFF)]))
            for mn in ("addq", "subq"):
                for sz in "bwl":
                    ds = [0, 2, 200]
                    groups.append(("disp.%s.%s.%s" % (b, mn, sz), [(setup(d) + ["move.l  #$%x, (a5)" % v], ["%s.%s  #%d, %d(%s)" % (mn, sz, q, d, b)], "capx") for d in ds for v in (0, 0x7F000000, 0xFFFFFFFF) for q in (1, 8)]))
            for mn in ("btst", "bset", "bclr", "bchg"):
                groups.append(("disp.%s.%s" % (b, mn), [(setup(d) + ["move.l  #$%x, (a5)" % v], ["%s  #%d, %d(%s)" % (mn, bit, d, b)], "capx") for d in (0, 1, -4, 200) for v in (0, 0xFFFFFFFF, 0xA5000000) for bit in (0, 3, 7)]))
            # (memory shifts d(An) are checked by memshift_model.py: qemu-m68k mistakes ASR for LSR
            #  whenever bit 3 of the EA field is set, which d(An) always does)
        # memory to memory, both operands with displacements (two bases, and the same base twice)
        for sz in "bwl":
            groups.append(("disp.move.%s.mem2mem" % sz, [(["lea     cellmid, a0", "lea     cellmid, a4", "lea     %d(a4), a5" % d2], ["move.%s  %d(a0), %d(a4)" % (sz, d1, d2)], "capx") for d1 in (0, 4, -8, 200) for d2 in (0, 8, -16, 128)]))
            groups.append(("disp.move.%s.mem2mem_same" % sz, [(["lea     cellmid, a3", "lea     %d(a3), a5" % d2], ["move.%s  %d(a3), %d(a3)" % (sz, d1, d2)], "capx") for d1 in (0, 4, -8) for d2 in (0, 8, -16)]))
        # An as the source or target of its own displacement
        groups.append(("disp.alias", [(["lea     cellmid, a0"], ["move.l  a0, %d(a0)" % d, "move.l  %d(a0), d2" % d, "lea     cellmid, a5", "move.l  a5, d5", "sub.l    d5, d2", "move.l  d2, d1"]) for d in (0, 4, -8, 200)] +
                                  [(["lea     cellmid, a0"], ["move.l  a0, %d(a0)" % d, "move.l  %d(a0), a0" % d, "move.l  a0, d2", "lea     cellmid, a5", "move.l  a5, d5", "sub.l    d5, d2", "move.l  d2, d1"]) for d in (0, 4, -8, 200)] +
                                  [(["lea     cellmid, a3"], ["move.l  a3, %d(a3)" % d, "move.l  %d(a3), a3" % d, "move.l  a3, d2", "lea     cellmid, a5", "move.l  a5, d5", "sub.l    d5, d2", "move.l  d2, d1"]) for d in (0, 4, -8, 200)]))
    if "misc" in which:
        groups.append(("swap", [(["move.l  #$%x, d1" % v], ["swap    d1"]) for v in VALUES]))
        for sz in "wl": groups.append(("ext.%s" % sz, [(["move.l  #$%x, d1" % v], ["ext.%s  d1" % sz]) for v in VALUES]))
    if "binary" in which:
        for mn in (os.environ.get("BIN","and,or,eor").split(",")):
            for sz in "bwl": groups.append(("%s.%s" % (mn, sz), binary_cases(mn, sz)))
    if "shift" in which:
        for mn in ("asl", "asr", "lsl", "lsr", "rol", "ror"):
            for sz in "bwl":
                for form in ("imm", "reg"): groups.append(("%s.%s.%s" % (mn, sz, form), shift_cases(mn, sz, form)))
    ARENA = "disp" in which
    for name, cases in groups:
        a, b = check(name.replace(" ", "").replace("(", "_").replace(")", "").replace(",", "_"), cases); ok += a; tot += b
    print("---- %d of %d (group, target) runs match real 68K" % (ok, tot))
    sys.exit(0 if ok == tot else 1)
