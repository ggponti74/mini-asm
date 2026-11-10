#!/bin/sh
# run.sh - mini-asm regression tests. Native ELF runtime tests are skipped on Windows.
#
#   tests/run.sh [path/to/mini-asm]        (default: ./mini-asm; `make test` calls this)
#
# 1. tests/main.asm, then every tests/sections/NN_name.asm as its own program.
#    Exit code 0 = pass, N = test number N failed (see the D3 convention in main.asm).
#    Names listed in tests/xfail.txt are expected to fail: shown as XFAIL, and an
#    unexpected pass is reported as XPASS (counted as a failure so the entry gets removed).
# 2. tests/errors/*.asm must fail to assemble; stderr must match the regex on
#    their first line ("; EXPECT: <extended regex>").
# 3. tests/arm/*.asm: ARM programs, run with qemu-arm when available (exit code must match "; EXIT: n").
# 4. tests/trap/*.asm: TRAP #0 services (files etc.), exit code 0 = pass, N = test N failed. They
#    run in a scratch directory (they create files), on the ELF target, on ARM with qemu-arm, and
#    as real PE executables with wine. tests/trap/pe_dispatch_test.c checks the PE dispatcher against fake
#    kernel32 functions when a 32-bit gcc (gcc -m32) works.
# 5. tests/main.asm must also assemble for the x86 (PE) target (assembly only).

ASM=${1:-./mini-asm}
DIR=$(cd "$(dirname "$0")" && pwd)

# Git for Windows can assemble ELF output, but Windows cannot execute Linux ELF
# binaries. Detect the native MinGW compiler and retain all assembly/byte tests.
WINDOWS_HOST=0
if gcc -dumpmachine 2>/dev/null | grep -qiE 'mingw|windows'; then
    WINDOWS_HOST=1
fi

# Paths embedded inside assembly source are not converted by Git Bash's
# automatic argument translation, so convert them explicitly for MinGW.
include_path() {
    if [ "$WINDOWS_HOST" -eq 1 ] && command -v cygpath >/dev/null 2>&1; then
        cygpath -m "$1"
    else
        printf '%s' "$1"
    fi
}

TMP=$(mktemp -d "${TMPDIR:-/tmp}/mini-asm-test.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT

pass=0; fail=0

# "main" includes every section, so it is expected to fail while any entry is listed.
xfail_listed() {
    test_name=$1
    case "$test_name" in
        *" (arm)") test_name=${test_name% (arm)} ;;
    esac
    if [ "$test_name" = main ]; then grep -qv '^#' "$DIR/xfail.txt" 2>/dev/null
    else grep -qx "$test_name" "$DIR/xfail.txt" 2>/dev/null; fi
}

report() {   # report <name> <exit code of the program>
    name=$1; code=$2
    if xfail_listed "$name"; then
        if [ "$code" -eq 0 ]; then
            echo "XPASS  $name   (passes now - remove it from tests/xfail.txt)"; fail=$((fail + 1))
        else
            echo "XFAIL  $name   (expected: test $code)"; pass=$((pass + 1))
        fi
    elif [ "$code" -eq 0 ]; then
        echo "PASS   $name"; pass=$((pass + 1))
    else
        echo "FAIL   $name   (test $code failed)"; fail=$((fail + 1))
    fi
}

run_program() {   # run_program <name> <source file>
    if ! "$ASM" -t elf -o "$TMP/prog" "$2" >"$TMP/asm.log" 2>&1; then
        echo "FAIL   $1   (did not assemble)"; sed 's/^/         /' "$TMP/asm.log" | grep '^ *Error' ; fail=$((fail + 1)); return
    fi
    if [ "$WINDOWS_HOST" -eq 1 ]; then
        echo "SKIP   $1   (Linux ELF executable cannot run on Windows)"
    else
        "$TMP/prog"; report "$1" $?
    fi
}

run_arm_program() {   # run_arm_program <name> <source file>
    if ! "$ASM" -t arm -o "$TMP/arm-prog" "$2" >"$TMP/asm.log" 2>&1; then
        echo "FAIL   $1 (arm)   (did not assemble)"; sed 's/^/         /' "$TMP/asm.log" | grep '^ *Error' ; fail=$((fail + 1)); return
    fi
    qemu-arm "$TMP/arm-prog"; report "$1 (arm)" $?
}

# 1. whole program, then each section standalone
run_program main "$DIR/main.asm"
for sec in "$DIR"/sections/*.asm; do
    name=$(basename "$sec" .asm)
    sec_path=$(include_path "$sec")
    common_path=$(include_path "$DIR/common.asm")
    {
        echo "start:"
        echo "        include \"$sec_path\""
        echo "        moveq   #0, d0"
        echo "        rts"
        echo "        include \"$common_path\""
    } > "$TMP/section.asm"
    run_program "$name" "$TMP/section.asm"
done

# 2. error cases
for src in "$DIR"/errors/*.asm; do
    name=errors/$(basename "$src" .asm)
    want=$(sed -n '1s/^; EXPECT: //p' "$src")
    if [ -z "$want" ]; then echo "FAIL   $name   (no '; EXPECT:' line)"; fail=$((fail + 1)); continue; fi
    if "$ASM" -t elf -o "$TMP/err" "$src" >/dev/null 2>"$TMP/err.log"; then
        echo "FAIL   $name   (assembled, but an error was expected)"; fail=$((fail + 1))
    elif grep -Eq "$want" "$TMP/err.log"; then
        echo "PASS   $name"; pass=$((pass + 1))
    else
        echo "FAIL   $name   (message did not match: $want)"; sed 's/^/         got: /' "$TMP/err.log"; fail=$((fail + 1))
    fi
done

# 3. ARM programs: run the shared x86 regression suite and ARM-specific tests with qemu-arm
if [ "$WINDOWS_HOST" -eq 0 ] && command -v qemu-arm >/dev/null 2>&1; then
    run_arm_program main "$DIR/main.asm"
    for sec in "$DIR"/sections/*.asm; do
        name=$(basename "$sec" .asm)
        sec_path=$(include_path "$sec")
        common_path=$(include_path "$DIR/common.asm")
        {
            echo "start:"
            echo "        include \"$sec_path\""
            echo "        moveq   #0, d0"
            echo "        rts"
            echo "        include \"$common_path\""
        } > "$TMP/arm-section.asm"
        run_arm_program "$name" "$TMP/arm-section.asm"
    done
    for src in "$DIR"/arm/*.asm; do
        name=arm/$(basename "$src" .asm)
        want=$(sed -n '1s/^; EXIT: //p' "$src")
        if ! "$ASM" -t arm -o "$TMP/arm.bin" "$src" >"$TMP/asm.log" 2>&1; then
            echo "FAIL   $name   (did not assemble)"; fail=$((fail + 1)); continue
        fi
        qemu-arm "$TMP/arm.bin" 2>/dev/null; got=$?
        if [ "$got" -eq "$want" ]; then echo "PASS   $name"; pass=$((pass + 1))
        else echo "FAIL   $name   (exit $got, expected $want)"; fail=$((fail + 1)); fi
    done
else
    if [ "$WINDOWS_HOST" -eq 1 ]; then
        echo "SKIP   arm/*   (Linux ARM executables cannot run on Windows)"
    else
        echo "SKIP   arm/*   (qemu-arm not installed)"
    fi
fi

# 4. TRAP #0 service tests
mkdir -p "$TMP/work"
# wine is only used if it can run a trivial PE program made by this assembler (exit code 7)
wine_ok=0
if command -v wine >/dev/null 2>&1; then
    export WINEPREFIX="$TMP/wineprefix" WINEDEBUG=-all
    printf 'start:  moveq   #7, d0\n        rts\n' >"$TMP/wine_probe.asm"
    if "$ASM" -t x86 -o "$TMP/wine_probe.exe" "$TMP/wine_probe.asm" >/dev/null 2>&1; then
        (cd "$TMP/work" && timeout 300 wine "$TMP/wine_probe.exe" >/dev/null 2>&1); [ $? -eq 7 ] && wine_ok=1
    fi
fi
for src in "$DIR"/trap/*.asm; do
    tname=trap/$(basename "$src" .asm)   # (report() overwrites $name)
    if ! "$ASM" -t elf -o "$TMP/trap.elf" "$src" >"$TMP/asm.log" 2>&1; then
        echo "FAIL   $tname   (did not assemble)"; sed 's/^/         /' "$TMP/asm.log" | grep '^ *Error'; fail=$((fail + 1)); continue
    fi
    if [ "$WINDOWS_HOST" -eq 1 ]; then
        echo "SKIP   $tname   (Linux ELF executable cannot run on Windows)"
    else
        (cd "$TMP/work" && "$TMP/trap.elf" >/dev/null); report "$tname" $?
    fi
    if [ "$WINDOWS_HOST" -eq 0 ] && command -v qemu-arm >/dev/null 2>&1; then
        if "$ASM" -t arm -o "$TMP/trap.arm" "$src" >"$TMP/asm.log" 2>&1; then
            (cd "$TMP/work" && qemu-arm "$TMP/trap.arm" >/dev/null 2>&1); report "$tname (arm)" $?
        elif grep -q "not supported" "$TMP/asm.log"; then
            echo "SKIP   $tname (arm)   (TRAP is not supported on the ARM target yet)"
        else
            echo "FAIL   $tname (arm)   (did not assemble)"; sed 's/^/         /' "$TMP/asm.log" | grep '^ *Error'; fail=$((fail + 1))
        fi
    fi
    if [ "$wine_ok" -eq 1 ]; then
        if "$ASM" -t x86 -o "$TMP/trap.exe" "$src" >"$TMP/asm.log" 2>&1; then
            (cd "$TMP/work" && timeout 300 wine "$TMP/trap.exe" >/dev/null 2>&1); report "$tname (pe/wine)" $?
        else
            echo "FAIL   $tname (pe/wine)   (did not assemble)"; sed 's/^/         /' "$TMP/asm.log" | grep '^ *Error'; fail=$((fail + 1))
        fi
    fi
done
[ "$wine_ok" -eq 1 ] || echo "SKIP   trap/* as PE   (no working wine)"
if gcc -m32 -x c -o "$TMP/m32" - >/dev/null 2>&1 <<'EOC'
int main(void) { return 0; }
EOC
then
    if gcc -m32 -Wall -Wextra -I"$DIR/../src" -o "$TMP/pe_test" "$DIR/trap/pe_dispatch_test.c" "$DIR/../src/core.c" >"$TMP/asm.log" 2>&1; then
        (cd "$TMP/work" && "$TMP/pe_test" </dev/null >"$TMP/pe.log" 2>&1); code=$?
        if [ "$code" -eq 0 ]; then echo "PASS   trap/pe_dispatcher (fake kernel32)"; pass=$((pass + 1))
        else echo "FAIL   trap/pe_dispatcher"; sed 's/^/         /' "$TMP/pe.log" | grep FAIL; fail=$((fail + 1)); fi
    else
        echo "FAIL   trap/pe_dispatcher   (did not compile)"; sed 's/^/         /' "$TMP/asm.log" | head -5; fail=$((fail + 1))
    fi
else
    echo "SKIP   trap/pe_dispatcher   (no 32-bit gcc)"
fi

# 5. other x86 output format: assembly only (a PE file cannot run here)
if "$ASM" -t x86 -o "$TMP/main.exe" "$DIR/main.asm" >/dev/null 2>&1; then
    echo "PASS   main (x86/PE assembles)"; pass=$((pass + 1))
else
    echo "FAIL   main (x86/PE does not assemble)"; fail=$((fail + 1))
fi

# 6. 8-bit CPU encodings: verify exact raw opcode bytes
printf 'nop\nret\n' > "$TMP/z80.asm"
if "$ASM" -t z80 -o "$TMP/z80.bin" "$TMP/z80.asm" >/dev/null 2>&1 &&
   [ "$(od -An -tx1 "$TMP/z80.bin" | tr -d ' \n')" = "00c9" ]; then
    echo "PASS   z80 opcode scaffold"; pass=$((pass + 1))
else
    echo "FAIL   z80 opcode scaffold"; fail=$((fail + 1))
fi
printf 'nop\nld a, 5\nld hl, 0x1234\nret\n' > "$TMP/z80_real.asm"
if "$ASM" -t z80 -o "$TMP/z80_real.bin" "$TMP/z80_real.asm" >/dev/null 2>&1 &&
   [ "$(od -An -tx1 "$TMP/z80_real.bin" | tr -d ' \n')" = "003e05213412c9" ]; then
    echo "PASS   z80 basic encoding"; pass=$((pass + 1))
else
    echo "FAIL   z80 basic encoding"; fail=$((fail + 1))
fi
cat > "$TMP/z80_ld_memory.asm" <<'EOF'
LD A,(0x1234)
LD (0x1234),A
LD (HL),0x42
LD A,(BC)
LD A,(DE)
LD (BC),A
LD (DE),A
LD B,(HL)
LD (HL),C
EOF
if "$ASM" -t z80 -o "$TMP/z80_ld_memory.bin" "$TMP/z80_ld_memory.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_ld_memory.bin" | tr -d ' \n')" = "3a341232341236420a1a02124671" ]; then
    echo "PASS   z80 LD memory addressing"; pass=$((pass + 1))
else
    echo "FAIL   z80 LD memory addressing"; fail=$((fail + 1))
fi
printf 'LD A,256\n' > "$TMP/z80_ld_bad_imm.asm"
if "$ASM" -t z80 -o "$TMP/z80_ld_bad_imm.bin" "$TMP/z80_ld_bad_imm.asm" >/dev/null 2>&1; then
    echo "FAIL   z80 rejects out-of-range 8-bit LD immediate"; fail=$((fail + 1))
else
    echo "PASS   z80 rejects out-of-range 8-bit LD immediate"; pass=$((pass + 1))
fi
printf 'LD A,(0x10000)\n' > "$TMP/z80_ld_bad_addr.asm"
if "$ASM" -t z80 -o "$TMP/z80_ld_bad_addr.bin" "$TMP/z80_ld_bad_addr.asm" >/dev/null 2>&1; then
    echo "FAIL   z80 rejects out-of-range absolute memory address"; fail=$((fail + 1))
else
    echo "PASS   z80 rejects out-of-range absolute memory address"; pass=$((pass + 1))
fi

cat > "$TMP/z80_control_flow.asm" <<'EOF'
JR start
JR NZ, start
JR Z, start
JR NC, start
JR C, start
DJNZ start
JP start
JP NZ, start
JP M, start
CALL start
CALL PE, start
RET
RET NZ
RET M
JP (HL)
start: NOP
EOF
if "$ASM" -t z80 -o "$TMP/z80_control_flow.bin" "$TMP/z80_control_flow.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_control_flow.bin" | tr -d ' \n')" = "181d201b2819301738151013c31f00c21f00fa1f00cd1f00ec1f00c9c0f8e900" ]; then
    echo "PASS   z80 conditional control flow"; pass=$((pass + 1))
else
    echo "FAIL   z80 conditional control flow"; fail=$((fail + 1))
fi
cat > "$TMP/z80_all_conditions.asm" <<'EOF'
JP NZ,0x1234
JP Z,0x1234
JP NC,0x1234
JP C,0x1234
JP PO,0x1234
JP PE,0x1234
JP P,0x1234
JP M,0x1234
CALL NZ,0x1234
CALL Z,0x1234
CALL NC,0x1234
CALL C,0x1234
CALL PO,0x1234
CALL PE,0x1234
CALL P,0x1234
CALL M,0x1234
RET NZ
RET Z
RET NC
RET C
RET PO
RET PE
RET P
RET M
EOF
if "$ASM" -t z80 -o "$TMP/z80_all_conditions.bin" "$TMP/z80_all_conditions.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_all_conditions.bin" | tr -d ' \n')" = "c23412ca3412d23412da3412e23412ea3412f23412fa3412c43412cc3412d43412dc3412e43412ec3412f43412fc3412c0c8d0d8e0e8f0f8" ]; then
    echo "PASS   z80 all conditional opcode families"; pass=$((pass + 1))
else
    echo "FAIL   z80 all conditional opcode families"; fail=$((fail + 1))
fi
printf 'JP 0x10000\n' > "$TMP/z80_bad_jump.asm"
if "$ASM" -t z80 -o "$TMP/z80_bad_jump.bin" "$TMP/z80_bad_jump.asm" >/dev/null 2>&1; then
    echo "FAIL   z80 rejects out-of-range JP target"; fail=$((fail + 1))
else
    echo "PASS   z80 rejects out-of-range JP target"; pass=$((pass + 1))
fi
printf 'JR PO, target\ndc.b 0\n' > "$TMP/z80_bad_condition.asm"
if "$ASM" -t z80 -o "$TMP/z80_bad_condition.bin" "$TMP/z80_bad_condition.asm" >/dev/null 2>&1; then
    echo "FAIL   z80 rejects unsupported JR condition"; fail=$((fail + 1))
else
    echo "PASS   z80 rejects unsupported JR condition"; pass=$((pass + 1))
fi

# Z80 relative branches: verify every JR condition and DJNZ with resolved
# forward labels, plus the exact signed displacement boundaries and wraparound.
cat > "$TMP/z80_relative_branch_matrix.asm" <<'EOF'
JR NZ,target
JR Z,target
JR NC,target
JR C,target
DJNZ target
target: NOP
EOF
if "$ASM" -t z80 -o "$TMP/z80_relative_branch_matrix.bin" "$TMP/z80_relative_branch_matrix.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_relative_branch_matrix.bin" | tr -d '[:space:]')" = "2008280630043802100000" ]; then
    echo "PASS   z80 all relative branch conditions with forward labels"
    pass=$((pass + 1))
else
    echo "FAIL   z80 all relative branch conditions with forward labels"
    fail=$((fail + 1))
fi

{
    echo 'JR target'
    i=0
    while [ "$i" -lt 127 ]; do echo 'dc.b 0'; i=$((i + 1)); done
    echo 'target: NOP'
} > "$TMP/z80-jr-forward-limit.asm"
if "$ASM" -t z80 -o "$TMP/z80-jr-forward-limit.bin" "$TMP/z80-jr-forward-limit.asm" >/dev/null 2>&1 &&
   [ "$(od -An -tx1 -N2 "$TMP/z80-jr-forward-limit.bin" | tr -d '[:space:]')" = "187f" ] &&
   [ "$(wc -c < "$TMP/z80-jr-forward-limit.bin" | tr -d ' ')" = "130" ]; then
    echo "PASS   z80 accepts +127 relative branch"
    pass=$((pass + 1))
else
    echo "FAIL   z80 accepts +127 relative branch"
    fail=$((fail + 1))
fi

{
    echo 'target:'
    i=0
    while [ "$i" -lt 126 ]; do echo 'dc.b 0'; i=$((i + 1)); done
    echo 'JR target'
} > "$TMP/z80-jr-backward-limit.asm"
if "$ASM" -t z80 -o "$TMP/z80-jr-backward-limit.bin" "$TMP/z80-jr-backward-limit.asm" >/dev/null 2>&1 &&
   [ "$(od -An -tx1 -j126 -N2 "$TMP/z80-jr-backward-limit.bin" | tr -d '[:space:]')" = "1880" ] &&
   [ "$(wc -c < "$TMP/z80-jr-backward-limit.bin" | tr -d ' ')" = "128" ]; then
    echo "PASS   z80 accepts -128 relative branch"
    pass=$((pass + 1))
else
    echo "FAIL   z80 accepts -128 relative branch"
    fail=$((fail + 1))
fi

for direction in forward backward; do
    if [ "$direction" = forward ]; then
        {
            echo 'JR target'
            i=0
            while [ "$i" -lt 128 ]; do echo 'dc.b 0'; i=$((i + 1)); done
            echo 'target: NOP'
        } > "$TMP/z80-jr-out-of-range.asm"
    else
        {
            echo 'target:'
            i=0
            while [ "$i" -lt 127 ]; do echo 'dc.b 0'; i=$((i + 1)); done
            echo 'JR target'
        } > "$TMP/z80-jr-out-of-range.asm"
    fi
    if "$ASM" -t z80 -o "$TMP/z80-jr-out-of-range.bin" "$TMP/z80-jr-out-of-range.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects $direction branch outside signed range"
        fail=$((fail + 1))
    else
        echo "PASS   z80 rejects $direction branch outside signed range"
        pass=$((pass + 1))
    fi
done

printf 'JR $0000\n' > "$TMP/z80-jr-wrap.asm"
if "$ASM" -t z80 --base 65534 -o "$TMP/z80-jr-wrap.bin" "$TMP/z80-jr-wrap.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80-jr-wrap.bin" | tr -d '[:space:]')" = "1800" ]; then
    echo "PASS   z80 relative branch wraps at 16-bit address boundary"
    pass=$((pass + 1))
else
    echo "FAIL   z80 relative branch wraps at 16-bit address boundary"
    fail=$((fail + 1))
fi
cat > "$TMP/z80_alu.asm" <<'EOF'
ADD A,B
ADD A,(HL)
ADC A,C
SUB D
SBC A,E
AND H
XOR L
OR (HL)
CP A
ADD HL,BC
ADD HL,SP
ADC HL,DE
SBC HL,HL
INC (HL)
DEC A
INC BC
DEC SP
EOF
if "$ASM" -t z80 -o "$TMP/z80_alu.bin" "$TMP/z80_alu.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_alu.bin" | tr -d ' \n')" = "808689929ba4adb6bf0939ed5aed62343d033b" ]; then
    echo "PASS   z80 arithmetic and logical register forms"; pass=$((pass + 1))
else
    echo "FAIL   z80 arithmetic and logical register forms"; fail=$((fail + 1))
fi
cat > "$TMP/z80_16bit_hl_arithmetic.asm" <<'EOF'
ADD HL,BC
ADD HL,DE
ADD HL,HL
ADD HL,SP
ADC HL,BC
ADC HL,DE
ADC HL,HL
ADC HL,SP
SBC HL,BC
SBC HL,DE
SBC HL,HL
SBC HL,SP
EOF
if "$ASM" -t z80 -o "$TMP/z80_16bit_hl_arithmetic.bin" "$TMP/z80_16bit_hl_arithmetic.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_16bit_hl_arithmetic.bin" | tr -d ' \n')" = "09192939ed4aed5aed6aed7aed42ed52ed62ed72" ]; then
    echo "PASS   z80 16-bit HL arithmetic pair matrix"; pass=$((pass + 1))
else
    echo "FAIL   z80 16-bit HL arithmetic pair matrix"; fail=$((fail + 1))
fi
cat > "$TMP/z80_alu_immediate.asm" <<'EOF'
ADD A,0x12
ADC A,0x34
SUB 0x56
SBC A,0x78
AND 0x9A
XOR 0xBC
OR 0xDE
CP 0xF0
EOF
if "$ASM" -t z80 -o "$TMP/z80_alu_immediate.bin" "$TMP/z80_alu_immediate.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_alu_immediate.bin" | tr -d ' \n')" = "c612ce34d656de78e69aeebcf6defef0" ]; then
    echo "PASS   z80 arithmetic and logical immediate forms"; pass=$((pass + 1))
else
    echo "FAIL   z80 arithmetic and logical immediate forms"; fail=$((fail + 1))
fi
cat > "$TMP/z80_rotate_shift.asm" <<'EOF'
RLCA
RRCA
RLA
RRA
RLC B
RRC C
RL D
RR E
SLA H
SRA L
SRL (HL)
CPL
DAA
SCF
CCF
EOF
if "$ASM" -t z80 -o "$TMP/z80_rotate_shift.bin" "$TMP/z80_rotate_shift.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_rotate_shift.bin" | tr -d ' \n')" = "070f171fcb00cb09cb12cb1bcb24cb2dcb3e2f27373f" ]; then
    echo "PASS   z80 accumulator rotates and CB shifts"; pass=$((pass + 1))
else
    echo "FAIL   z80 accumulator rotates and CB shifts"; fail=$((fail + 1))
fi
cat > "$TMP/z80_block_transfer_search.asm" <<'EOF'
LDI
LDIR
LDD
LDDR
CPI
CPIR
CPD
CPDR
EOF
if "$ASM" -t z80 -o "$TMP/z80_block_transfer_search.bin" "$TMP/z80_block_transfer_search.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_block_transfer_search.bin" | tr -d ' \n')" = "eda0edb0eda8edb8eda1edb1eda9edb9" ]; then
    echo "PASS   z80 block transfer and search instructions"; pass=$((pass + 1))
else
    echo "FAIL   z80 block transfer and search instructions"; fail=$((fail + 1))
fi
printf 'LDI A\n' > "$TMP/z80_bad_block_operand.asm"
if "$ASM" -t z80 -o "$TMP/z80_bad_block_operand.bin" "$TMP/z80_bad_block_operand.asm" >/dev/null 2>&1; then
    echo "FAIL   z80 rejects operands on block instructions"; fail=$((fail + 1))
else
    echo "PASS   z80 rejects operands on block instructions"; pass=$((pass + 1))
fi
cat > "$TMP/z80_io.asm" <<'EOF'
IN A,(0x12)
OUT (0x34),A
IN B,(C)
IN C,(C)
IN D,(C)
IN E,(C)
IN H,(C)
IN L,(C)
IN A,(C)
IN F,(C)
OUT (C),B
OUT (C),C
OUT (C),D
OUT (C),E
OUT (C),H
OUT (C),L
OUT (C),A
OUT (C),0
EOF
if "$ASM" -t z80 -o "$TMP/z80_io.bin" "$TMP/z80_io.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_io.bin" | tr -d ' \n')" = "db12d334ed40ed48ed50ed58ed60ed68ed78ed70ed41ed49ed51ed59ed61ed69ed79ed71" ]; then
    echo "PASS   z80 IN/OUT immediate and (C) forms"; pass=$((pass + 1))
else
    echo "FAIL   z80 IN/OUT immediate and (C) forms"; fail=$((fail + 1))
fi
cat > "$TMP/z80_block_io.asm" <<'EOF'
INI
INIR
IND
INDR
OUTI
OTIR
OUTD
OTDR
EOF
if "$ASM" -t z80 -o "$TMP/z80_block_io.bin" "$TMP/z80_block_io.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_block_io.bin" | tr -d ' \n')" = "eda2edb2edaaedbaeda3edb3edabedbb" ]; then
    echo "PASS   z80 block I/O instructions"; pass=$((pass + 1))
else
    echo "FAIL   z80 block I/O instructions"; fail=$((fail + 1))
fi
expect_z80_io_failure() {   # expect_z80_io_failure <source>
    printf '%s\n' "$1" > "$TMP/z80-io-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-io-invalid.bin" "$TMP/z80-io-invalid.asm" >"$TMP/asm.log" 2>&1; then
        echo "FAIL   z80 rejects invalid I/O form: $1"; fail=$((fail + 1))
    else
        echo "PASS   z80 rejects invalid I/O form"; pass=$((pass + 1))
    fi
}
expect_z80_io_failure 'IN B,(0x12)'
expect_z80_io_failure 'OUT (0x12),B'
expect_z80_io_failure 'OUT (C),F'
expect_z80_io_failure 'IN IXH,(C)'
expect_z80_io_failure 'IN IYL,(C)'
expect_z80_io_failure 'OUT (C),IXH'
expect_z80_io_failure 'OUT (C),IYL'
expect_z80_io_failure 'OUT (C),1'
expect_z80_io_failure 'IN A,(0x100)'
expect_z80_io_failure 'OUT (0x100),A'
cat > "$TMP/z80_ed_special.asm" <<'EOF'
NEG
RETN
RETI
IM 0
IM 1
IM 2
RRD
RLD
LD I,A
LD R,A
LD A,I
LD A,R
LD (0x1234),BC
LD (0x2345),DE
LD (0x3456),HL
LD (0x4567),SP
LD BC,(0x1234)
LD DE,(0x2345)
LD HL,(0x3456)
LD SP,(0x4567)
EOF
expected="ed44ed45ed4ded46ed56ed5eed67ed6fed47ed4fed57ed5fed433412ed534523225634ed736745ed4b3412ed5b45232a5634ed7b6745"
if ! "$ASM" -t z80 -o "$TMP/z80_ed_special.bin" "$TMP/z80_ed_special.asm" >"$TMP/asm.log" 2>&1; then
    echo "FAIL   z80 ED special: assembler error"
    sed 's/^/         /' "$TMP/asm.log"
    fail=$((fail + 1))
else
    got=$(od -An -v -tx1 "$TMP/z80_ed_special.bin" | tr -d '[:space:]')
    if [ "$got" = "$expected" ]; then
        echo "PASS   z80 ED special and absolute pair instructions"
        pass=$((pass + 1))
    else
        echo "FAIL   z80 ED special: unexpected bytes"
        echo "         expected: $expected"
        echo "         got:      $got"
        fail=$((fail + 1))
    fi
fi
cat > "$TMP/z80_index.asm" <<'EOF'
LD IX,0x1234
LD IY,0x5678
INC IX
DEC IY
ADD IX,BC
ADD IX,DE
ADD IX,IX
ADD IX,SP
ADD IY,BC
ADD IY,DE
ADD IY,IY
ADD IY,SP
PUSH IX
POP IY
LD SP,HL
LD SP,IX
LD (0x2000),IX
LD IY,(0x3000)
JP (IX)
JP (IY)
LD A,(IX+5)
LD (IX-2),A
LD B,(IY)
LD (IY+127),C
LD (IX-128),0xA5
ADD A,(IX+1)
ADC A,(IY-1)
SUB (IX+2)
SBC A,(IY+3)
AND (IX)
XOR (IY)
OR (IX+4)
CP (IY-4)
INC (IX+6)
DEC (IY-6)
IX_OFF EQU 7
IY_OFF EQU 3
LD C,(IX+IX_OFF)
LD D,(IY-IY_OFF)
EOF
expected="dd213412fd217856dd23fd2bdd09dd19dd29dd39fd09fd19fd29fd39dde5fde1f9ddf9dd220020fd2a0030dde9fde9dd7e05dd77fefd4600fd717fdd3680a5dd8601fd8effdd9602fd9e03dda600fdae00ddb604fdbefcdd3406fd35fadd4e07fd56fd"
if ! "$ASM" -t z80 -o "$TMP/z80_index.bin" "$TMP/z80_index.asm" >"$TMP/asm.log" 2>&1; then
    echo "FAIL   z80 IX/IY and indexed-memory: assembler error"
    sed 's/^/         /' "$TMP/asm.log"
    fail=$((fail + 1))
else
    got=$(od -An -v -tx1 "$TMP/z80_index.bin" | tr -d '[:space:]')
    if [ "$got" = "$expected" ]; then
        echo "PASS   z80 IX/IY pairs and indexed-memory operations"
        pass=$((pass + 1))
    else
        echo "FAIL   z80 IX/IY and indexed-memory: unexpected bytes"
        echo "         expected: $expected"
        echo "         got:      $got"
        fail=$((fail + 1))
    fi
fi
cat > "$TMP/z80_sll.asm" <<'EOF'
SLL B
SLL (HL)
SLL A
SLL (IX+1)
SLL (IY-2)
EOF
expected="cb30cb36cb37ddcb0136fdcbfe36"
if "$ASM" -t z80 -o "$TMP/z80_sll.bin" "$TMP/z80_sll.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_sll.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 undocumented SLL forms"
    pass=$((pass + 1))
else
    echo "FAIL   z80 undocumented SLL forms"
    fail=$((fail + 1))
fi
printf '%s\n' 'SLL IXH' > "$TMP/z80-sll-invalid.asm"
if "$ASM" -t z80 -o "$TMP/z80-sll-invalid.bin" "$TMP/z80-sll-invalid.asm" >/dev/null 2>&1; then
    echo "FAIL   z80 rejects SLL index-half register"
    fail=$((fail + 1))
else
    echo "PASS   z80 rejects SLL index-half register"
    pass=$((pass + 1))
fi

cat > "$TMP/z80_index_extended.asm" <<'EOF'
RLC (IX+1)
BIT 3,(IX-2)
RES 5,(IY+4)
SET 7,(IY)
RES 5,(IY+4),B
SET 7,(IY),A
SRA (IY-1)
LD IXH,0x12
LD IXL,0x34
LD IYH,0x56
LD IYL,0x78
LD A,IXH
LD IYL,A
LD IXH,IXL
LD IYH,B
INC IXH
DEC IXL
INC IYH
DEC IYL
ADD A,IXH
ADC A,IXL
SUB IYH
SBC A,IYL
AND IXH
XOR IXL
OR IYH
CP IYL
EOF
expected="ddcb0106ddcbfe5efdcb04aefdcb00fefdcb04a8fdcb00fffdcbff2edd2612dd2e34fd2656fd2e78dd7cfd6fdd65fd60dd24dd2dfd24fd2ddd84dd8dfd94fd9ddda4ddadfdb4fdbd"
if ! "$ASM" -t z80 -o "$TMP/z80_index_extended.bin" "$TMP/z80_index_extended.asm" >"$TMP/asm.log" 2>&1; then
    echo "FAIL   z80 DDCB/FDCB and index-half: assembler error"
    sed 's/^/         /' "$TMP/asm.log"
    fail=$((fail + 1))
else
    got=$(od -An -v -tx1 "$TMP/z80_index_extended.bin" | tr -d '[:space:]')
    if [ "$got" = "$expected" ]; then
        echo "PASS   z80 DDCB/FDCB and IXH/IXL/IYH/IYL"
        pass=$((pass + 1))
    else
        echo "FAIL   z80 DDCB/FDCB and index-half: unexpected bytes"
        echo "         expected: $expected"
        echo "         got:      $got"
        fail=$((fail + 1))
    fi
fi
expect_z80_index_extended_failure() {
    printf '%s\n' "$1" > "$TMP/z80-index-extended-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-index-extended-invalid.bin" "$TMP/z80-index-extended-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects invalid index extension: $1"; fail=$((fail + 1))
    else
        echo "PASS   z80 rejects invalid index extension"; pass=$((pass + 1))
    fi
}
expect_z80_index_extended_failure 'LD IXH,(IX+1)'
expect_z80_index_extended_failure 'LD (IX+1),IXH'
expect_z80_index_extended_failure 'LD IXH,IYL'
expect_z80_index_extended_failure 'LD H,IXH'
expect_z80_index_extended_failure 'LD IXH,H'
expect_z80_index_extended_failure 'LD L,IXL'
expect_z80_index_extended_failure 'LD IYL,L'
expect_z80_index_extended_failure 'LD IYH,H'
expect_z80_index_extended_failure 'RLC IXH'
expect_z80_index_extended_failure 'BIT 2,(IY+128)'
expect_z80_index_failure() {
    printf '%s\n' "$1" > "$TMP/z80-index-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-index-invalid.bin" "$TMP/z80-index-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects invalid IX/IY form: $1"; fail=$((fail + 1))
    else
        echo "PASS   z80 rejects invalid IX/IY form"; pass=$((pass + 1))
    fi
}
expect_z80_index_failure 'LD A,(IX+128)'
expect_z80_index_failure 'LD A,(IY-129)'
expect_z80_index_failure 'ADD IX,IY'
expect_z80_index_failure 'LD (IX+1),IX'
expect_z80_index_failure 'LD (IX+1),(IY+1)'
expect_z80_index_failure 'LD IX,0x10000'

cat > "$TMP/z80_stack_pairs.asm" <<'EOF'
PUSH BC
PUSH DE
PUSH HL
PUSH AF
PUSH IX
PUSH IY
POP BC
POP DE
POP HL
POP AF
POP IX
POP IY
EOF
expected="c5d5e5f5dde5fde5c1d1e1f1dde1fde1"
if "$ASM" -t z80 -o "$TMP/z80_stack_pairs.bin" "$TMP/z80_stack_pairs.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_stack_pairs.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 PUSH/POP supported register pairs"
    pass=$((pass + 1))
else
    echo "FAIL   z80 PUSH/POP supported register pairs"
    fail=$((fail + 1))
fi
for bad in "PUSH SP" "POP SP" "PUSH IXH" "POP IYL"; do
    printf '%s\n' "$bad" > "$TMP/z80-stack-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-stack-invalid.bin" "$TMP/z80-stack-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects invalid stack pair: $bad"
        fail=$((fail + 1))
    else
        echo "PASS   z80 rejects invalid stack pair"
        pass=$((pass + 1))
    fi
done
expect_z80_ed_failure() {
    printf '%s
' "$1" > "$TMP/z80-ed-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-ed-invalid.bin" "$TMP/z80-ed-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects invalid ED form: $1"; fail=$((fail + 1))
    else
        echo "PASS   z80 rejects invalid ED form"; pass=$((pass + 1))
    fi
}
expect_z80_ed_failure 'IM 3'
expect_z80_ed_failure 'IM -1'
expect_z80_ed_failure 'LD I,B'
expect_z80_ed_failure 'LD (0x10000),BC'
expect_z80_ed_failure 'LD AF,(0x1234)'
cat > "$TMP/z80_bit_ops.asm" <<'EOF'
BIT 0,B
BIT 7,A
BIT 2,C
BIT 2,D
BIT 2,E
BIT 2,H
BIT 2,L
BIT 3,(HL)
RES 0,C
RES 7,(HL)
SET 0,D
SET 7,A
SET 3,(HL)
EOF
if "$ASM" -t z80 -o "$TMP/z80_bit_ops.bin" "$TMP/z80_bit_ops.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_bit_ops.bin" | tr -d ' \n')" = "cb40cb7fcb51cb52cb53cb54cb55cb5ecb81cbbecbc2cbffcbde" ]; then
    echo "PASS   z80 BIT/RES/SET register and (HL) forms"; pass=$((pass + 1))
else
    echo "FAIL   z80 BIT/RES/SET register and (HL) forms"; fail=$((fail + 1))
fi
expect_z80_failure() {   # expect_z80_failure <source>
    printf '%s\n' "$1" > "$TMP/z80-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-invalid.bin" "$TMP/z80-invalid.asm" >"$TMP/asm.log" 2>&1; then
        echo "FAIL   z80 rejects invalid bit form: $1"; fail=$((fail + 1))
    else
        echo "PASS   z80 rejects invalid bit form"; pass=$((pass + 1))
    fi
}
expect_z80_failure 'BIT 8,A'
expect_z80_failure 'RES -1,(HL)'
expect_z80_failure 'SET 3,BC'
printf 'ADD A,256\n' > "$TMP/z80_bad_alu_imm.asm"
if "$ASM" -t z80 -o "$TMP/z80_bad_alu_imm.bin" "$TMP/z80_bad_alu_imm.asm" >/dev/null 2>&1; then
    echo "FAIL   z80 rejects out-of-range ALU immediate"; fail=$((fail + 1))
else
    echo "PASS   z80 rejects out-of-range ALU immediate"; pass=$((pass + 1))
fi
printf 'ADD DE,BC\n' > "$TMP/z80_bad_alu_dest.asm"
if "$ASM" -t z80 -o "$TMP/z80_bad_alu_dest.bin" "$TMP/z80_bad_alu_dest.asm" >/dev/null 2>&1; then
    echo "FAIL   z80 rejects unsupported 16-bit ALU destination"; fail=$((fail + 1))
else
    echo "PASS   z80 rejects unsupported 16-bit ALU destination"; pass=$((pass + 1))
fi
printf 'nop\nrts\n' > "$TMP/6501.asm"
if "$ASM" -t 6501 -o "$TMP/6501.bin" "$TMP/6501.asm" >/dev/null 2>&1 &&
   [ "$(od -An -tx1 "$TMP/6501.bin" | tr -d ' \n')" = "ea60" ]; then
    echo "PASS   6501 NOP and RTS"; pass=$((pass + 1))
else
    echo "FAIL   6501 NOP and RTS"; fail=$((fail + 1))
fi
printf 'lda #$42\nsta $10\n' > "$TMP/6501_core.asm"
if "$ASM" -t 6501 -o "$TMP/6501_core.bin" "$TMP/6501_core.asm" >/dev/null 2>&1 &&
   [ "$(od -An -tx1 "$TMP/6501_core.bin" | tr -d ' \n')" = "a9428510" ]; then
    echo "PASS   6501 immediate and zero-page"; pass=$((pass + 1))
else
    echo "FAIL   6501 immediate and zero-page"; fail=$((fail + 1))
fi
check_6501_bytes() {   # check_6501_bytes <name> <source> <expected hex>
    name=$1
    expected=$3
    if "$ASM" -t 6501 -o "$TMP/6501.bin" "$2" >"$TMP/asm.log" 2>&1; then
        got=$(od -An -v -tx1 "$TMP/6501.bin" | tr -d ' \n')
        if [ "$got" = "$expected" ]; then
            echo "PASS   $name"; pass=$((pass + 1))
        else
            echo "FAIL   $name   (unexpected bytes)"
            echo "         expected: $expected"
            echo "         got:      $got"
            fail=$((fail + 1))
        fi
    else
        echo "FAIL   $name   (did not assemble)"
        sed 's/^/         /' "$TMP/asm.log" | grep 'Error'
        fail=$((fail + 1))
    fi
}
check_6501_bytes "6501 official implied and accumulator" "$DIR/6501/implied.asm" \
    "004060ea08284868aa8aa898ba9ae8cac88818385878b8d8f80a4a2a6a"
check_6501_bytes "6501 zero-page boundary selection" "$DIR/6501/zero_page_boundary.asm" \
    "a5ffad0001b5ffbd0001"
# If an instruction has no matching zero-page form, a short numeric
# address must still use its legal absolute form.
printf 'JMP $10\nJSR $10\nLDA $10,Y\nADC $10,Y\nSTA $10,Y\n' > "$TMP/6501-absolute-fallback.asm"
check_6501_bytes "6501 selects available absolute forms for short addresses" "$TMP/6501-absolute-fallback.asm" \
    "4c1000201000b91000791000991000"
printf 'LDA ($10), Y\n' > "$TMP/6501-indirect-spacing.asm"
check_6501_bytes "6501 accepts whitespace in indirect-indexed syntax" "$TMP/6501-indirect-spacing.asm" "b110"
cat > "$TMP/6501-indexed-boundaries.asm" <<'EOF'
LDA $00,X
LDA $FF,X
LDA $0100,X
LDX $FF,Y
LDY $FF,X
STA $FF,X
STX $FF,Y
STY $FF,X
EOF
check_6501_bytes "6501 indexed addressing boundaries" "$TMP/6501-indexed-boundaries.asm" \
    "b500b5ffbd0001b6ffb4ff95ff96ff94ff"
check_6501_bytes "6501 forward label stays absolute" "$DIR/6501/forward_label_absolute.asm" \
    "ad03014141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414141414160"
check_6501_bytes "6501 official addressing modes" "$DIR/6501/addressing.asm" \
    "6912651275126d34127d3412793412611271122912251235122d34123d341239341221123112c912c512d512cd3412dd3412d93412c112d1124912451255124d34125d3412593412411251120912051215120d34121d341219341201121112e912e512f512ed3412fd3412f93412e112f112ad1b01a912a512b512ad3412bd3412b93412a112b112a120b120851295128d34129d341299341281129112a212a612b612ae3412be3412861296128e3412a012a412b412ac3412bc3412841294128c341224122c3412e012e412ec3412c012c412cc3412061216120e34121e3412461256124e34125e3412261236122e34123e3412661276126e34127e3412e612f612ee3412fe3412c612d612ce3412de34124c34126c3412201b01ea"
check_6501_bytes "6501 relative branches" "$DIR/6501/branches.asm" \
    "101030fc900cb0f8f008d0f4500470f0800060"
expect_6501_failure() {   # expect_6501_failure <source>
    printf '%s\n' "$1" > "$TMP/6501-invalid.asm"
    if "$ASM" -t 6501 -o "$TMP/6501-invalid.bin" "$TMP/6501-invalid.asm" >"$TMP/asm.log" 2>&1; then
        echo "FAIL   6501 rejects: $1"; fail=$((fail + 1))
    else
        echo "PASS   6501 rejects invalid form"; pass=$((pass + 1))
    fi
}
expect_6501_failure 'LDA #$100'
expect_6501_failure 'LDA $10,A'
expect_6501_failure 'LDX ($10),Y'
expect_6501_failure 'LDA ($10)'
expect_6501_failure 'STA #$01'
expect_6501_failure 'LDA ($100,X)'
expect_6501_failure 'LDA ($20,Y)'
expect_6501_failure 'JMP ($20,X)'
expect_6501_failure 'LDA $10000'
expect_6501_failure 'ASL D0'
expect_6501_failure 'BNE -1'
expect_6501_failure 'STA $10, X, Y'
expect_6501_failure 'STA ($10,Y)'
expect_6501_failure 'LDX $10,X'
expect_6501_failure 'LDY $10,Y'
expect_6501_failure 'STX $10,X'
expect_6501_failure 'STY $10,Y'
{
    echo 'BNE far'
    i=0
    while [ "$i" -lt 128 ]; do
        echo 'dc.b 0'
        i=$((i + 1))
    done
    echo 'far: NOP'
} > "$TMP/6501-far-branch.asm"
if "$ASM" -t 6501 -o "$TMP/6501-far-branch.bin" "$TMP/6501-far-branch.asm" >"$TMP/asm.log" 2>&1; then
    echo "FAIL   6501 rejects out-of-range branch"; fail=$((fail + 1))
else
    echo "PASS   6501 rejects out-of-range branch"; pass=$((pass + 1))
fi
{
    echo 'BNE far'
    i=0
    while [ "$i" -lt 127 ]; do
        echo 'dc.b 0'
        i=$((i + 1))
    done
    echo 'far: NOP'
} > "$TMP/6501-forward-limit.asm"
if "$ASM" -t 6501 -o "$TMP/6501-forward-limit.bin" "$TMP/6501-forward-limit.asm" >"$TMP/asm.log" 2>&1 &&
   [ "$(od -An -tx1 -N2 "$TMP/6501-forward-limit.bin" | tr -d ' \n')" = "d07f" ] &&
   [ "$(wc -c < "$TMP/6501-forward-limit.bin" | tr -d ' ')" = "130" ]; then
    echo "PASS   6501 accepts +127 branch"; pass=$((pass + 1))
else
    echo "FAIL   6501 accepts +127 branch"; fail=$((fail + 1))
fi
{
    echo 'back:'
    i=0
    while [ "$i" -lt 126 ]; do
        echo 'dc.b 0'
        i=$((i + 1))
    done
    echo 'BNE back'
} > "$TMP/6501-backward-limit.asm"
if "$ASM" -t 6501 -o "$TMP/6501-backward-limit.bin" "$TMP/6501-backward-limit.asm" >"$TMP/asm.log" 2>&1 &&
   [ "$(od -An -tx1 -j126 "$TMP/6501-backward-limit.bin" | tr -d ' \n')" = "d080" ]; then
    echo "PASS   6501 accepts -128 branch"; pass=$((pass + 1))
else
    echo "FAIL   6501 accepts -128 branch"; fail=$((fail + 1))
fi

printf 'BNE $0000\n' > "$TMP/6501-wrap-branch.asm"
if "$ASM" -t 6501 --base 65534 -o "$TMP/6501-wrap-branch.bin" "$TMP/6501-wrap-branch.asm" >"$TMP/asm.log" 2>&1 &&
   [ "$(od -An -tx1 "$TMP/6501-wrap-branch.bin" | tr -d ' \n')" = "d000" ]; then
    echo "PASS   6501 branch wraps at 16-bit address boundary"; pass=$((pass + 1))
else
    echo "FAIL   6501 branch wraps at 16-bit address boundary"; fail=$((fail + 1))
    sed 's/^/         /' "$TMP/asm.log" | grep 'Error'
fi

# Code labels on the 6501 wrap with its 16-bit address bus, including
# branch targets, absolute operands and zero-page indirect pointers.
check_6501_wrap_bytes() {   # check_6501_wrap_bytes <name> <source> <expected hex>
    name=$1
    if "$ASM" -t 6501 --base 65535 -o "$TMP/6501-wrap.bin" "$2" >"$TMP/asm.log" 2>&1 &&
       [ "$(od -An -tx1 "$TMP/6501-wrap.bin" | tr -d ' \n')" = "$3" ]; then
        echo "PASS   $name"; pass=$((pass + 1))
    else
        echo "FAIL   $name"; sed 's/^/         /' "$TMP/asm.log" | grep 'Error'
        fail=$((fail + 1))
    fi
}
printf 'BNE target\ntarget: NOP\n' > "$TMP/6501-wrap-label-branch.asm"
check_6501_wrap_bytes "6501 branch to label across address wrap" "$TMP/6501-wrap-label-branch.asm" "d000ea"
printf 'NOP\ntarget: JMP target\n' > "$TMP/6501-wrap-label-jump.asm"
check_6501_wrap_bytes "6501 absolute label across address wrap" "$TMP/6501-wrap-label-jump.asm" "ea4c0000"
printf 'LDA (target,X)\ntarget: NOP\n' > "$TMP/6501-wrap-label-indirect.asm"
check_6501_wrap_bytes "6501 indirect label across address wrap" "$TMP/6501-wrap-label-indirect.asm" "a101ea"
printf 'dc.w target\ntarget: NOP\n' > "$TMP/6501-wrap-label-data.asm"
check_6501_wrap_bytes "6501 data directive label across address wrap" "$TMP/6501-wrap-label-data.asm" "0001ea"

cat > "$TMP/z80_exchange.asm" <<'EOF'
EX AF,AF'
EX DE,HL
EX (SP),HL
EX (SP),IX
EX (SP),IY
EXX
EOF
if "$ASM" -t z80 -o "$TMP/z80_exchange.bin" "$TMP/z80_exchange.asm" >"$TMP/asm.log" 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_exchange.bin" | tr -d '[:space:]')" = "08ebe3dde3fde3d9" ]; then
    echo "PASS   z80 exchange instructions"
    pass=$((pass + 1))
else
    echo "FAIL   z80 exchange instructions"
    sed 's/^/         /' "$TMP/asm.log"
    fail=$((fail + 1))
fi
expect_z80_exchange_failure() {
    printf '%s\n' "$1" > "$TMP/z80-exchange-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-exchange-invalid.bin" "$TMP/z80-exchange-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects invalid exchange form: $1"; fail=$((fail + 1))
    else
        echo "PASS   z80 rejects invalid exchange form"; pass=$((pass + 1))
    fi
}
expect_z80_exchange_failure 'EX'
expect_z80_exchange_failure 'EX BC,DE'
expect_z80_exchange_failure 'EX (SP),AF'
expect_z80_exchange_failure 'EX AF,AF'
expect_z80_exchange_failure 'EXX A'

cat > "$TMP/z80_interrupt_control.asm" <<'EOF'
DI
EI
DI
EOF
if "$ASM" -t z80 -o "$TMP/z80_interrupt_control.bin" "$TMP/z80_interrupt_control.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_interrupt_control.bin" | tr -d '[:space:]')" = "f3fbf3" ]; then
    echo "PASS   z80 interrupt control instructions"
    pass=$((pass + 1))
else
    echo "FAIL   z80 interrupt control instructions"
    fail=$((fail + 1))
fi
for bad in "DI A" "EI 1"; do
    printf '%s\n' "$bad" > "$TMP/z80-interrupt-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-interrupt-invalid.bin" "$TMP/z80-interrupt-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects operands on DI/EI: $bad"
        fail=$((fail + 1))
    else
        echo "PASS   z80 rejects operands on DI/EI"
        pass=$((pass + 1))
    fi
done

cat > "$TMP/z80_rst.asm" <<'EOF'
RST 0
RST 8
RST 0x10
RST 0x18
RST 0x20
RST 0x28
RST 0x30
RST 0x38
EOF
if "$ASM" -t z80 -o "$TMP/z80_rst.bin" "$TMP/z80_rst.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_rst.bin" | tr -d '[:space:]')" = "c7cfd7dfe7eff7ff" ]; then
    echo "PASS   z80 RST vectors"
    pass=$((pass + 1))
else
    echo "FAIL   z80 RST vectors"
    fail=$((fail + 1))
fi
for bad in "RST 1" "RST 7" "RST 0x40" "RST -8"; do
    printf '%s\n' "$bad" > "$TMP/z80-rst-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-rst-invalid.bin" "$TMP/z80-rst-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects invalid RST vector: $bad"
        fail=$((fail + 1))
    else
        echo "PASS   z80 rejects invalid RST vector"
        pass=$((pass + 1))
    fi
done
for bad in "RST A" "RST (0x08)" "RST destination"; do
    printf '%s\n' "$bad" > "$TMP/z80-rst-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-rst-invalid.bin" "$TMP/z80-rst-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects non-numeric RST operand: $bad"
        fail=$((fail + 1))
    else
        echo "PASS   z80 rejects non-numeric RST operand"
        pass=$((pass + 1))
    fi
done

cat > "$TMP/z80_indirect_jp.asm" <<'EOF'
JP (HL)
JP (IX)
JP (IY)
EOF
if "$ASM" -t z80 -o "$TMP/z80_indirect_jp.bin" "$TMP/z80_indirect_jp.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_indirect_jp.bin" | tr -d '[:space:]')" = "e9dde9fde9" ]; then
    echo "PASS   z80 indirect JP through HL/IX/IY"
    pass=$((pass + 1))
else
    echo "FAIL   z80 indirect JP through HL/IX/IY"
    fail=$((fail + 1))
fi
printf '%s\n' 'JP (IX+1)' > "$TMP/z80-indirect-jp-invalid.asm"
if "$ASM" -t z80 -o "$TMP/z80-indirect-jp-invalid.bin" "$TMP/z80-indirect-jp-invalid.asm" >/dev/null 2>&1; then
    echo "FAIL   z80 rejects displaced indirect JP"
    fail=$((fail + 1))
else
    echo "PASS   z80 rejects displaced indirect JP"
    pass=$((pass + 1))
fi

cat > "$TMP/z80_sp_relative.asm" <<'EOF'
ADD SP,1
ADD SP,-128
ADD SP,127
LD HL,SP+5
LD HL,SP-2
LD HL,SP+0
EOF
if "$ASM" -t z80 -o "$TMP/z80_sp_relative.bin" "$TMP/z80_sp_relative.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_sp_relative.bin" | tr -d '[:space:]')" = "e801e880e87ff805f8fef800" ]; then
    echo "PASS   z80 SP-relative ADD and LD forms"
    pass=$((pass + 1))
else
    echo "FAIL   z80 SP-relative ADD and LD forms"
    fail=$((fail + 1))
fi
for bad in "ADD SP,128" "ADD SP,-129" "LD HL,SP+128" "LD HL,SP-129"; do
    printf '%s\n' "$bad" > "$TMP/z80-sp-relative-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-sp-relative-invalid.bin" "$TMP/z80-sp-relative-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects out-of-range SP-relative operand: $bad"
        fail=$((fail + 1))
    else
        echo "PASS   z80 rejects out-of-range SP-relative operand"
        pass=$((pass + 1))
    fi
done

echo "----"

cat > "$TMP/z80_indexed_result_registers.asm" <<'EOF'
RLC (IX+1),B
SRA (IY-2),A
RES 3,(IX+4),C
SET 7,(IY-1),L
EOF
expected="ddcb0100fdcbfe2fddcb0499fdcbfffd"
if "$ASM" -t z80 -o "$TMP/z80_indexed_result_registers.bin" "$TMP/z80_indexed_result_registers.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_indexed_result_registers.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 indexed rotate/shift and RES/SET result registers"
    pass=$((pass + 1))
else
    echo "FAIL   z80 indexed rotate/shift and RES/SET result registers"
    fail=$((fail + 1))
fi
expect_z80_indexed_result_failure() {
    printf '%s\n' "$1" > "$TMP/z80-indexed-result-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-indexed-result-invalid.bin" "$TMP/z80-indexed-result-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects invalid indexed result form: $1"; fail=$((fail + 1))
    else
        echo "PASS   z80 rejects invalid indexed result form"; pass=$((pass + 1))
    fi
}
expect_z80_indexed_result_failure 'RLC (IX+1),IXH'
expect_z80_indexed_result_failure 'RES 3,(IY+1),IYL'
expect_z80_indexed_result_failure 'RLC B,A'
expect_z80_indexed_result_failure 'RES 3,B,A'

# Broad Z80 ALU, 16-bit arithmetic, and INC/DEC matrices. These cover every
# ordinary 8-bit register encoding, immediate ALU opcode, legal pair source,
# index-half form, and signed indexed-memory boundary.
cat > "$TMP/z80_alu_register_matrix.asm" <<'EOF'
ADD A,B
ADD A,C
ADD A,D
ADD A,E
ADD A,H
ADD A,L
ADD A,(HL)
ADD A,A
ADC A,B
ADC A,C
ADC A,D
ADC A,E
ADC A,H
ADC A,L
ADC A,(HL)
ADC A,A
SUB B
SUB C
SUB D
SUB E
SUB H
SUB L
SUB (HL)
SUB A
SBC A,B
SBC A,C
SBC A,D
SBC A,E
SBC A,H
SBC A,L
SBC A,(HL)
SBC A,A
AND B
AND C
AND D
AND E
AND H
AND L
AND (HL)
AND A
XOR B
XOR C
XOR D
XOR E
XOR H
XOR L
XOR (HL)
XOR A
OR B
OR C
OR D
OR E
OR H
OR L
OR (HL)
OR A
CP B
CP C
CP D
CP E
CP H
CP L
CP (HL)
CP A
ADD A,0x11
ADC A,0x22
SUB 0x33
SBC A,0x44
AND 0x55
XOR 0x66
OR 0x77
CP 0x88
EOF
if "$ASM" -t z80 -o "$TMP/z80_alu_register_matrix.bin" "$TMP/z80_alu_register_matrix.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_alu_register_matrix.bin" | tr -d '[:space:]')" = "808182838485868788898a8b8c8d8e8f909192939495969798999a9b9c9d9e9fa0a1a2a3a4a5a6a7a8a9aaabacadaeafb0b1b2b3b4b5b6b7b8b9babbbcbdbebfc611ce22d633de44e655ee66f677fe88" ]; then
    echo "PASS   z80 complete 8-bit ALU register and immediate matrix"; pass=$((pass + 1))
else
    echo "FAIL   z80 complete 8-bit ALU register and immediate matrix"; fail=$((fail + 1))
fi

cat > "$TMP/z80_alu_pair_matrix.asm" <<'EOF'
ADD HL,BC
ADD HL,DE
ADD HL,HL
ADD HL,SP
ADC HL,BC
ADC HL,DE
ADC HL,HL
ADC HL,SP
SBC HL,BC
SBC HL,DE
SBC HL,HL
SBC HL,SP
ADD IX,BC
ADD IX,DE
ADD IX,IX
ADD IX,SP
ADD IY,BC
ADD IY,DE
ADD IY,IY
ADD IY,SP
EOF
if "$ASM" -t z80 -o "$TMP/z80_alu_pair_matrix.bin" "$TMP/z80_alu_pair_matrix.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_alu_pair_matrix.bin" | tr -d '[:space:]')" = "09192939ed4aed5aed6aed7aed42ed52ed62ed72dd09dd19dd29dd39fd09fd19fd29fd39" ]; then
    echo "PASS   z80 complete 16-bit HL/IX/IY arithmetic matrix"; pass=$((pass + 1))
else
    echo "FAIL   z80 complete 16-bit HL/IX/IY arithmetic matrix"; fail=$((fail + 1))
fi

cat > "$TMP/z80_incdec_matrix.asm" <<'EOF'
INC B
INC C
INC D
INC E
INC H
INC L
INC (HL)
INC A
DEC B
DEC C
DEC D
DEC E
DEC H
DEC L
DEC (HL)
DEC A
INC IXH
INC IXL
INC IYH
INC IYL
DEC IXH
DEC IXL
DEC IYH
DEC IYL
INC BC
INC DE
INC HL
INC SP
DEC BC
DEC DE
DEC HL
DEC SP
INC IX
INC IY
DEC IX
DEC IY
INC (IX-128)
INC (IX+127)
INC (IY-128)
INC (IY+127)
DEC (IX-128)
DEC (IX+127)
DEC (IY-128)
DEC (IY+127)
EOF
if "$ASM" -t z80 -o "$TMP/z80_incdec_matrix.bin" "$TMP/z80_incdec_matrix.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_incdec_matrix.bin" | tr -d '[:space:]')" = "040c141c242c343c050d151d252d353ddd24dd2cfd24fd2cdd25dd2dfd25fd2d031323330b1b2b3bdd23fd23dd2bfd2bdd3480dd347ffd3480fd347fdd3580dd357ffd3580fd357f" ]; then
    echo "PASS   z80 complete INC/DEC register, pair, and indexed matrix"; pass=$((pass + 1))
else
    echo "FAIL   z80 complete INC/DEC register, pair, and indexed matrix"; fail=$((fail + 1))
fi

for bad in "INC AF" "DEC AF" "INC IXH, A" "DEC (IX+128)" "INC (IY-129)" \
           "ADD IX,IY" "ADD IY,IX" "ADC IX,BC" "SBC IY,DE" \
           "ADD A,SP" "ADC A,IX" "SUB A,B" "AND IXH, A"; do
    printf '%s\n' "$bad" > "$TMP/z80-alu-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-alu-invalid.bin" "$TMP/z80-alu-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects invalid arithmetic form: $bad"; fail=$((fail + 1))
    else
        echo "PASS   z80 rejects invalid arithmetic form"; pass=$((pass + 1))
    fi
done


# Exhaustive Z80 LD register-to-register matrix. The (HL),(HL) combination
# is intentionally omitted because byte 0x76 is HALT, not an LD instruction.
z80_reg_name() {
    case "$1" in
        0) printf B ;; 1) printf C ;; 2) printf D ;; 3) printf E ;;
        4) printf H ;; 5) printf L ;; 6) printf '(HL)' ;; 7) printf A ;;
    esac
}
: > "$TMP/z80_ld_register_matrix.asm"
expected=""
for dst in 0 1 2 3 4 5 6 7; do
    for src in 0 1 2 3 4 5 6 7; do
        if [ "$dst" -eq 6 ] && [ "$src" -eq 6 ]; then
            continue
        fi
        d=$(z80_reg_name "$dst")
        s=$(z80_reg_name "$src")
        printf 'LD %s,%s\n' "$d" "$s" >> "$TMP/z80_ld_register_matrix.asm"
        expected="${expected}$(printf '%02x' "$((0x40 + dst * 8 + src))")"
    done
done
if "$ASM" -t z80 -o "$TMP/z80_ld_register_matrix.bin" "$TMP/z80_ld_register_matrix.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_ld_register_matrix.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 exhaustive 8-bit LD register/memory matrix"; pass=$((pass + 1))
else
    echo "FAIL   z80 exhaustive 8-bit LD register/memory matrix"; fail=$((fail + 1))
fi

cat > "$TMP/z80_ld_immediate_matrix.asm" <<'EOF'
LD B,0x12
LD C,0x23
LD D,0x34
LD E,0x45
LD H,0x56
LD L,0x67
LD (HL),0x78
LD A,0x89
LD BC,0x1234
LD DE,0x2345
LD HL,0x3456
LD SP,0x4567
LD IX,0x5678
LD IY,0x6789
EOF
expected="06120e2316341e4526562e6736783e89013412114523215634316745dd217856fd218967"
if "$ASM" -t z80 -o "$TMP/z80_ld_immediate_matrix.bin" "$TMP/z80_ld_immediate_matrix.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_ld_immediate_matrix.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 complete 8-bit and 16-bit LD immediate matrix"; pass=$((pass + 1))
else
    echo "FAIL   z80 complete 8-bit and 16-bit LD immediate matrix"; fail=$((fail + 1))
fi

for bad in "LD (BC),B" "LD (DE),C" "LD BC,A" "LD AF,0x1234" \
           "LD IXH,(IX+1)" "LD IYL,(IY-1)" "LD (IX+1),IXH" \
           "LD (IY-1),IYL" "LD (HL),(HL)" "LD SP,AF"; do
    printf '%s\n' "$bad" > "$TMP/z80-ld-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-ld-invalid.bin" "$TMP/z80-ld-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects invalid LD form: $bad"; fail=$((fail + 1))
    else
        echo "PASS   z80 rejects invalid LD form"; pass=$((pass + 1))
    fi
done


# Complete undocumented IXH/IXL and IYH/IYL register-transfer matrices.
z80_index_half_name() {
    case "$1" in
        0) printf B ;; 1) printf C ;; 2) printf D ;; 3) printf E ;;
        4) printf '%sH' "$2" ;; 5) printf '%sL' "$2" ;; 7) printf A ;;
    esac
}
: > "$TMP/z80_index_half_ld_matrix.asm"
expected=""
for pair in IX IY; do
    if [ "$pair" = IX ]; then prefix=dd; else prefix=fd; fi
    for dst in 0 1 2 3 4 5 7; do
        for src in 0 1 2 3 4 5 7; do
            d=$(z80_index_half_name "$dst" "$pair")
            s=$(z80_index_half_name "$src" "$pair")
            printf 'LD %s,%s\n' "$d" "$s" >> "$TMP/z80_index_half_ld_matrix.asm"
            opcode=$((0x40 + dst * 8 + src))
            if [ "$dst" -eq 4 ] || [ "$dst" -eq 5 ] ||
               [ "$src" -eq 4 ] || [ "$src" -eq 5 ]; then
                expected="${expected}${prefix}"
            fi
            expected="${expected}$(printf '%02x' "$opcode")"
        done
    done
done
if "$ASM" -t z80 -o "$TMP/z80_index_half_ld_matrix.bin" "$TMP/z80_index_half_ld_matrix.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_index_half_ld_matrix.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 exhaustive IXH/IXL and IYH/IYL LD matrices"; pass=$((pass + 1))
else
    echo "FAIL   z80 exhaustive IXH/IXL and IYH/IYL LD matrices"; fail=$((fail + 1))
fi

cat > "$TMP/z80_index_half_immediate.asm" <<'EOF'
LD IXH,0x12
LD IXL,0x34
LD IYH,0x56
LD IYL,0x78
EOF
expected="dd2612dd2e34fd2656fd2e78"
if "$ASM" -t z80 -o "$TMP/z80_index_half_immediate.bin" "$TMP/z80_index_half_immediate.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_index_half_immediate.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 IX/IY index-half immediate loads"; pass=$((pass + 1))
else
    echo "FAIL   z80 IX/IY index-half immediate loads"; fail=$((fail + 1))
fi

# Indexed-memory loads/stores cover every ordinary register field at both
# signed displacement extremes, plus immediate stores through IX and IY.
z80_plain_reg_name() {
    case "$1" in
        0) printf B ;; 1) printf C ;; 2) printf D ;; 3) printf E ;;
        4) printf H ;; 5) printf L ;; 7) printf A ;;
    esac
}
: > "$TMP/z80_indexed_ld_matrix.asm"
expected=""
for pair in IX IY; do
    if [ "$pair" = IX ]; then prefix=dd; else prefix=fd; fi
    for reg in 0 1 2 3 4 5 7; do
        name=$(z80_plain_reg_name "$reg")
        printf 'LD (%s-128),%s\n' "$pair" "$name" >> "$TMP/z80_indexed_ld_matrix.asm"
        printf 'LD %s,(%s+127)\n' "$name" "$pair" >> "$TMP/z80_indexed_ld_matrix.asm"
        expected="${expected}${prefix}$(printf '%02x80' $((0x70 + reg)))"
        expected="${expected}${prefix}$(printf '%02x7f' $((0x46 + reg * 8)))"
    done
done
printf 'LD (IX+0),0xA5\nLD (IY-1),0x5A\n' >> "$TMP/z80_indexed_ld_matrix.asm"
expected="${expected}dd3600a5fd36ff5a"
if "$ASM" -t z80 -o "$TMP/z80_indexed_ld_matrix.bin" "$TMP/z80_indexed_ld_matrix.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_indexed_ld_matrix.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 complete IX/IY indexed LD register and immediate matrix"; pass=$((pass + 1))
else
    echo "FAIL   z80 complete IX/IY indexed LD register and immediate matrix"; fail=$((fail + 1))
fi


# Absolute-memory transfers for every Z80 register pair, special-register
# transfers, stack-pointer copies, and accumulator BC/DE indirect forms.
cat > "$TMP/z80_absolute_and_special_ld.asm" <<'EOF'
LD (0x1234),BC
LD (0x1234),DE
LD (0x1234),HL
LD (0x1234),SP
LD (0x1234),IX
LD (0x1234),IY
LD BC,(0x1234)
LD DE,(0x1234)
LD HL,(0x1234)
LD SP,(0x1234)
LD IX,(0x1234)
LD IY,(0x1234)
LD SP,HL
LD SP,IX
LD SP,IY
LD A,(BC)
LD A,(DE)
LD (BC),A
LD (DE),A
LD A,(0x1234)
LD (0x1234),A
LD I,A
LD R,A
LD A,I
LD A,R
EOF
expected="ed433412ed533412223412ed733412dd223412fd223412ed4b3412ed5b34122a3412ed7b3412dd2a3412fd2a3412f9ddf9fdf90a1a02123a3412323412ed47ed4fed57ed5f"
if "$ASM" -t z80 -o "$TMP/z80_absolute_and_special_ld.bin" "$TMP/z80_absolute_and_special_ld.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_absolute_and_special_ld.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 absolute, pair, stack-pointer, and special-register LD forms"; pass=$((pass + 1))
else
    echo "FAIL   z80 absolute, pair, stack-pointer, and special-register LD forms"; fail=$((fail + 1))
fi

for bad in "LD AF,(0x1234)" "LD (0x1234),AF" "LD I,B" "LD B,I" "LD R,C" "LD C,R" \
           "LD BC,(65536)" "LD (65536),DE"; do
    printf '%s\n' "$bad" > "$TMP/z80-absolute-ld-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-absolute-ld-invalid.bin" "$TMP/z80-absolute-ld-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects invalid absolute/special LD form: $bad"; fail=$((fail + 1))
    else
        echo "PASS   z80 rejects invalid absolute/special LD form"; pass=$((pass + 1))
    fi
done


# Exhaustive Z80 CB-prefixed opcode coverage. The ordinary matrix checks all
# 256 encodings: eight rotate/shift operations and every BIT/RES/SET bit/register
# combination. The indexed matrix checks DDCB/FDCB displacement boundaries,
# including all documented and undocumented result-register encodings.
z80_cb_reg_name() {
    case "$1" in
        0) printf B ;; 1) printf C ;; 2) printf D ;; 3) printf E ;;
        4) printf H ;; 5) printf L ;; 6) printf '(HL)' ;; 7) printf A ;;
    esac
}
: > "$TMP/z80_cb_opcode_matrix.asm"
expected=""
for mnemonic in RLC RRC RL RR SLA SRA SLL SRL; do
    case "$mnemonic" in
        RLC) base=0x00 ;; RRC) base=0x08 ;; RL) base=0x10 ;; RR) base=0x18 ;;
        SLA) base=0x20 ;; SRA) base=0x28 ;; SLL) base=0x30 ;; SRL) base=0x38 ;;
    esac
    for reg in 0 1 2 3 4 5 6 7; do
        name=$(z80_cb_reg_name "$reg")
        printf '%s %s\n' "$mnemonic" "$name" >> "$TMP/z80_cb_opcode_matrix.asm"
        expected="${expected}cb$(printf '%02x' $((base + reg)))"
    done
done
for mnemonic in BIT RES SET; do
    case "$mnemonic" in BIT) base=0x40 ;; RES) base=0x80 ;; SET) base=0xC0 ;; esac
    for bit in 0 1 2 3 4 5 6 7; do
        for reg in 0 1 2 3 4 5 6 7; do
            name=$(z80_cb_reg_name "$reg")
            printf '%s %s,%s\n' "$mnemonic" "$bit" "$name" >> "$TMP/z80_cb_opcode_matrix.asm"
            expected="${expected}cb$(printf '%02x' $((base + bit * 8 + reg)))"
        done
    done
done
if "$ASM" -t z80 -o "$TMP/z80_cb_opcode_matrix.bin" "$TMP/z80_cb_opcode_matrix.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_cb_opcode_matrix.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 exhaustive 256-opcode CB matrix"
    pass=$((pass + 1))
else
    echo "FAIL   z80 exhaustive 256-opcode CB matrix"
    fail=$((fail + 1))
fi

: > "$TMP/z80_indexed_cb_matrix.asm"
expected=""
for pair in IX IY; do
    if [ "$pair" = IX ]; then prefix=dd; else prefix=fd; fi
    for disp in -128 127; do
        if [ "$disp" -lt 0 ]; then disp_byte=80; else disp_byte=7f; fi
        for mnemonic in RLC RRC RL RR SLA SRA SLL SRL; do
            case "$mnemonic" in
                RLC) base=0x00 ;; RRC) base=0x08 ;; RL) base=0x10 ;; RR) base=0x18 ;;
                SLA) base=0x20 ;; SRA) base=0x28 ;; SLL) base=0x30 ;; SRL) base=0x38 ;;
            esac
            for reg in 0 1 2 3 4 5 6 7; do
                if [ "$reg" -eq 6 ]; then
                    printf '%s (%s%+d)\n' "$mnemonic" "$pair" "$disp" >> "$TMP/z80_indexed_cb_matrix.asm"
                else
                    name=$(z80_cb_reg_name "$reg")
                    printf '%s (%s%+d),%s\n' "$mnemonic" "$pair" "$disp" "$name" >> "$TMP/z80_indexed_cb_matrix.asm"
                fi
                expected="${expected}${prefix}cb${disp_byte}$(printf '%02x' $((base + reg)))"
            done
        done
        for bit in 0 1 2 3 4 5 6 7; do
            printf 'BIT %s,(%s%+d)\n' "$bit" "$pair" "$disp" >> "$TMP/z80_indexed_cb_matrix.asm"
            expected="${expected}${prefix}cb${disp_byte}$(printf '%02x' $((0x40 + bit * 8 + 6)))"
            for mnemonic in RES SET; do
                if [ "$mnemonic" = RES ]; then base=0x80; else base=0xC0; fi
                for reg in 0 1 2 3 4 5 6 7; do
                    if [ "$reg" -eq 6 ]; then
                        printf '%s %s,(%s%+d)\n' "$mnemonic" "$bit" "$pair" "$disp" >> "$TMP/z80_indexed_cb_matrix.asm"
                    else
                        name=$(z80_cb_reg_name "$reg")
                        printf '%s %s,(%s%+d),%s\n' "$mnemonic" "$bit" "$pair" "$disp" "$name" >> "$TMP/z80_indexed_cb_matrix.asm"
                    fi
                    expected="${expected}${prefix}cb${disp_byte}$(printf '%02x' $((base + bit * 8 + reg)))"
                done
            done
        done
    done
done
if "$ASM" -t z80 -o "$TMP/z80_indexed_cb_matrix.bin" "$TMP/z80_indexed_cb_matrix.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_indexed_cb_matrix.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 exhaustive DDCB/FDCB indexed opcode matrix"
    pass=$((pass + 1))
else
    echo "FAIL   z80 exhaustive DDCB/FDCB indexed opcode matrix"
    fail=$((fail + 1))
fi


# Exhaustive Z80 control-flow, stack-pair, restart-vector, and interrupt-mode
# matrices. These validate all condition-code encodings and legal stack pairs.
cat > "$TMP/z80_control_flow_matrix.asm" <<'EOF'
JP NZ,0x1234
JP Z,0x1234
JP NC,0x1234
JP C,0x1234
JP PO,0x1234
JP PE,0x1234
JP P,0x1234
JP M,0x1234
JP 0x1234
JP (HL)
JP (IX)
JP (IY)
CALL NZ,0x1234
CALL Z,0x1234
CALL NC,0x1234
CALL C,0x1234
CALL PO,0x1234
CALL PE,0x1234
CALL P,0x1234
CALL M,0x1234
CALL 0x1234
RET NZ
RET Z
RET NC
RET C
RET PO
RET PE
RET P
RET M
RET
EOF
expected="c23412ca3412d23412da3412e23412ea3412f23412fa3412c33412e9dde9fde9c43412cc3412d43412dc3412e43412ec3412f43412fc3412cd3412c0c8d0d8e0e8f0f8c9"
if "$ASM" -t z80 -o "$TMP/z80_control_flow_matrix.bin" "$TMP/z80_control_flow_matrix.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_control_flow_matrix.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 complete conditional JP/CALL/RET and indirect JP matrix"; pass=$((pass + 1))
else
    echo "FAIL   z80 complete conditional JP/CALL/RET and indirect JP matrix"; fail=$((fail + 1))
fi

cat > "$TMP/z80_stack_pair_matrix.asm" <<'EOF'
PUSH BC
PUSH DE
PUSH HL
PUSH AF
PUSH IX
PUSH IY
POP BC
POP DE
POP HL
POP AF
POP IX
POP IY
EOF
expected="c5d5e5f5dde5fde5c1d1e1f1dde1fde1"
if "$ASM" -t z80 -o "$TMP/z80_stack_pair_matrix.bin" "$TMP/z80_stack_pair_matrix.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_stack_pair_matrix.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 complete PUSH/POP pair and index-register matrix"; pass=$((pass + 1))
else
    echo "FAIL   z80 complete PUSH/POP pair and index-register matrix"; fail=$((fail + 1))
fi

cat > "$TMP/z80_rst_im_matrix.asm" <<'EOF'
RST 0
RST 8
RST 16
RST 24
RST 32
RST 40
RST 48
RST 56
IM 0
IM 1
IM 2
EOF
expected="c7cfd7dfe7eff7ffed46ed56ed5e"
if "$ASM" -t z80 -o "$TMP/z80_rst_im_matrix.bin" "$TMP/z80_rst_im_matrix.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_rst_im_matrix.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 complete RST vector and interrupt-mode matrix"; pass=$((pass + 1))
else
    echo "FAIL   z80 complete RST vector and interrupt-mode matrix"; fail=$((fail + 1))
fi

for bad in "PUSH SP" "POP SP" "PUSH A" "POP BC,DE" "RST 3" "RST 0x40" "RST -8" "IM 3" "IM -1" "JP PO" "CALL NZ" "RET IX"; do
    printf '%s\n' "$bad" > "$TMP/z80-control-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-control-invalid.bin" "$TMP/z80-control-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects invalid control/stack form: $bad"; fail=$((fail + 1))
    else
        echo "PASS   z80 rejects invalid control/stack form"; pass=$((pass + 1))
    fi
done


# Verify pass-two resolution of forward and backward symbols in absolute
# control-flow, immediate-pair, and memory-transfer operands.
cat > "$TMP/z80_absolute_symbol_matrix.asm" <<'EOF'
backward: NOP
LD HL,backward
LD A,(backward)
LD (backward),A
JP backward
CALL backward
JP forward
CALL forward
LD HL,forward
LD A,(forward)
LD (forward),A
forward: NOP
EOF
expected="002100003a0000320000c30000cd0000c31f00cd1f00211f003a1f00321f0000"
if "$ASM" -t z80 -o "$TMP/z80_absolute_symbol_matrix.bin" "$TMP/z80_absolute_symbol_matrix.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_absolute_symbol_matrix.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 forward/backward absolute symbol resolution matrix"; pass=$((pass + 1))
else
    echo "FAIL   z80 forward/backward absolute symbol resolution matrix"; fail=$((fail + 1))
fi

# Absolute Z80 addresses are unsigned 16-bit values. Exercise both endpoints
# across accumulator, pair, index-register, and store/load encodings.
cat > "$TMP/z80_absolute_address_limits.asm" <<'EOF'
LD BC,(0)
LD DE,(65535)
LD (0),HL
LD (65535),SP
LD A,(0)
LD (65535),A
LD IX,(65535)
LD (0),IY
EOF
expected="ed4b0000ed5bffff220000ed73ffff3a000032ffffdd2afffffd220000"
if "$ASM" -t z80 -o "$TMP/z80_absolute_address_limits.bin" "$TMP/z80_absolute_address_limits.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_absolute_address_limits.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 absolute address zero/65535 boundary matrix"; pass=$((pass + 1))
else
    echo "FAIL   z80 absolute address zero/65535 boundary matrix"; fail=$((fail + 1))
fi

printf 'JP missing_z80_symbol\n' > "$TMP/z80_unresolved_absolute_symbol.asm"
if "$ASM" -t z80 -o "$TMP/z80_unresolved_absolute_symbol.bin" "$TMP/z80_unresolved_absolute_symbol.asm" >/dev/null 2>&1; then
    echo "FAIL   z80 rejects unresolved absolute symbol"; fail=$((fail + 1))
else
    echo "PASS   z80 rejects unresolved absolute symbol"; pass=$((pass + 1))
fi


# Cover the remaining fixed Z80 accumulator operations, HALT, and ED-prefixed
# single-purpose instructions. These are all fixed encodings with no operands.
cat > "$TMP/z80_fixed_and_ed_special.asm" <<'EOF'
HALT
RLCA
RRCA
RLA
RRA
CPL
DAA
SCF
CCF
NEG
RETN
RETI
RRD
RLD
EOF
expected="76070f171f2f27373fed44ed45ed4ded67ed6f"
if "$ASM" -t z80 -o "$TMP/z80_fixed_and_ed_special.bin" "$TMP/z80_fixed_and_ed_special.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_fixed_and_ed_special.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 HALT, accumulator operations, and ED special instructions"
    pass=$((pass + 1))
else
    echo "FAIL   z80 HALT, accumulator operations, and ED special instructions"
    fail=$((fail + 1))
fi

# Fixed no-operand instructions must reject both stray operands and missing
# operands on the one-operand ED instruction IM.
for bad in "HALT A" "RLCA 1" "RRCA A" "RLA 0" "RRA B" \
           "CPL A" "DAA 1" "SCF A" "CCF 0" "NEG A" \
           "RETN 0" "RETI A" "RRD B" "RLD 1" \
           "IM" "IM 0,1"; do
    printf '%s\n' "$bad" > "$TMP/z80-fixed-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-fixed-invalid.bin" "$TMP/z80-fixed-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects invalid fixed/ED instruction form: $bad"
        fail=$((fail + 1))
    else
        echo "PASS   z80 rejects invalid fixed/ED instruction form"
        pass=$((pass + 1))
    fi
done


# Fixed Z80 control instructions: NOP, DI, EI, and EXX.
cat > "$TMP/z80_fixed_control.asm" <<'EOF'
NOP
DI
EI
EXX
EOF
expected="00f3fbd9"
if "$ASM" -t z80 -o "$TMP/z80_fixed_control.bin" "$TMP/z80_fixed_control.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_fixed_control.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 NOP, DI, EI, and EXX fixed encodings"
    pass=$((pass + 1))
else
    echo "FAIL   z80 NOP, DI, EI, and EXX fixed encodings"
    fail=$((fail + 1))
fi

for bad in "NOP A" "DI 1" "EI B" "EXX 0"; do
    printf '%s\n' "$bad" > "$TMP/z80-fixed-control-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-fixed-control-invalid.bin" "$TMP/z80-fixed-control-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects invalid fixed control instruction form: $bad"
        fail=$((fail + 1))
    else
        echo "PASS   z80 rejects invalid fixed control instruction form"
        pass=$((pass + 1))
    fi
done


# Z80 uses the shared data directives and EQU symbols. Verify byte values,
# big-endian word/long emission, and pass-two resolution of forward labels.
cat > "$TMP/z80_directives.asm" <<'EOF'
BYTE EQU $7f
WORD EQU $1234
LONG EQU $12345678
start: dc.b BYTE,1
dc.w start,WORD,end
dc.l LONG
end: NOP
EOF
expected="7f0100001234000c1234567800"
if "$ASM" -t z80 -o "$TMP/z80_directives.bin" "$TMP/z80_directives.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_directives.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 EQU, dc.b/dc.w/dc.l, and forward-label data directives"
    pass=$((pass + 1))
else
    echo "FAIL   z80 EQU, dc.b/dc.w/dc.l, and forward-label data directives"
    fail=$((fail + 1))
fi

for bad in "dc.b 256" "dc.b -129" "dc.w 65536" "dc.w -32769" "dc.l 4294967296" "dc.l -2147483649" 'dc.w "text"' "dc.b missing_z80_data_label"; do
    printf '%s\n' "$bad" > "$TMP/z80-directive-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-directive-invalid.bin" "$TMP/z80-directive-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects invalid data directive: $bad"
        fail=$((fail + 1))
    else
        echo "PASS   z80 rejects invalid data directive"
        pass=$((pass + 1))
    fi
done



# Z80 I/O: exercise every documented register form for IN/OUT through (C),
# immediate-port accumulator forms, and the two commonly supported
# undocumented encodings.
cat > "$TMP/z80_io_matrix.asm" <<'EOF'
IN B,(C)
IN C,(C)
IN D,(C)
IN E,(C)
IN H,(C)
IN L,(C)
IN A,(C)
OUT (C),B
OUT (C),C
OUT (C),D
OUT (C),E
OUT (C),H
OUT (C),L
OUT (C),A
IN A,(0x12)
OUT (0x34),A
IN F,(C)
OUT (C),0
EOF
expected="ed40ed48ed50ed58ed60ed68ed78ed41ed49ed51ed59ed61ed69ed79db12d334ed70ed71"
if "$ASM" -t z80 -o "$TMP/z80_io_matrix.bin" "$TMP/z80_io_matrix.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_io_matrix.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 IN/OUT register, immediate-port, and undocumented forms"
    pass=$((pass + 1))
else
    echo "FAIL   z80 IN/OUT register, immediate-port, and undocumented forms"
    fail=$((fail + 1))
fi

for bad in "IN IXH,(C)" "IN IXL,(C)" "IN IYH,(C)" "IN IYL,(C)" \
           "OUT (C),IXH" "OUT (C),IXL" "OUT (C),IYH" "OUT (C),IYL" \
           "IN B,(0x12)" "OUT (0x12),B" "IN A,(256)" "OUT (-1),A" \
           "IN A,C" "OUT C,A" "IN B,(256)"; do
    printf '%s\n' "$bad" > "$TMP/z80-io-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-io-invalid.bin" "$TMP/z80-io-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects invalid I/O form: $bad"
        fail=$((fail + 1))
    else
        echo "PASS   z80 rejects invalid I/O form"
        pass=$((pass + 1))
    fi
done


# Z80 symbol resolution across immediate, absolute, indexed, and control-flow
# operands. This exercises pass-two values in several distinct encodings.
cat > "$TMP/z80_symbol_operands.asm" <<'EOF'
BYTE_VALUE EQU $42
WORD_VALUE EQU $1234
INDEX_DISP EQU 5
BIT_VALUE EQU 3
start:
    LD A,BYTE_VALUE
    LD HL,WORD_VALUE
    ADD A,BYTE_VALUE
    LD (WORD_VALUE),A
    LD A,(WORD_VALUE)
    LD B,(IX+INDEX_DISP)
    BIT BIT_VALUE,(IY-INDEX_DISP)
    JP target
    JR target
target:
    NOP
EOF
expected="3e42213412c6423234123a3412dd4605fdcbfb5ec31900180000"
if "$ASM" -t z80 -o "$TMP/z80_symbol_operands.bin" "$TMP/z80_symbol_operands.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_symbol_operands.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 EQU symbols in immediate, absolute, indexed, and branch operands"
    pass=$((pass + 1))
else
    echo "FAIL   z80 EQU symbols in immediate, absolute, indexed, and branch operands"
    fail=$((fail + 1))
fi

for bad in "BYTE_VALUE EQU 256
LD A,BYTE_VALUE" "INDEX_DISP EQU 128
LD A,(IX+INDEX_DISP)" "WORD_VALUE EQU 65536
JP WORD_VALUE"; do
    printf '%s\n' "$bad" > "$TMP/z80-symbol-range-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-symbol-range-invalid.bin" "$TMP/z80-symbol-range-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects out-of-range EQU operand"
        fail=$((fail + 1))
    else
        echo "PASS   z80 rejects out-of-range EQU operand"
        pass=$((pass + 1))
    fi
done


# Exercise EQU values in every Z80 absolute-memory pair form, plus indexed
# displacements, indexed immediate stores, and conditional control-flow targets.
cat > "$TMP/z80_symbol_addressing.asm" <<'EOF'
ADDR EQU $2345
BYTE_VALUE EQU $7f
DISP EQU 2
LD (ADDR),BC
LD BC,(ADDR)
LD (ADDR),DE
LD DE,(ADDR)
LD (ADDR),HL
LD HL,(ADDR)
LD (ADDR),SP
LD SP,(ADDR)
LD A,(ADDR)
LD (ADDR),A
LD A,(IX-DISP)
LD (IY+DISP),BYTE_VALUE
JP NZ,ADDR
CALL PE,ADDR
EOF
expected="ed434523ed4b4523ed534523ed5b45232245232a4523ed734523ed7b45233a4523324523dd7efe fd36027fc24523ec4523"
expected=$(printf '%s' "$expected" | tr -d ' ')
if "$ASM" -t z80 -o "$TMP/z80_symbol_addressing.bin" "$TMP/z80_symbol_addressing.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_symbol_addressing.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 EQU symbols in absolute pair, indexed, and conditional forms"
    pass=$((pass + 1))
else
    echo "FAIL   z80 EQU symbols in absolute pair, indexed, and conditional forms"
    fail=$((fail + 1))
fi

for bad in "BAD_ADDR EQU 65536
LD BC,(BAD_ADDR)" "BAD_ADDR EQU 65536
LD (BAD_ADDR),SP" "BAD_DISP EQU -129
LD A,(IY+BAD_DISP)" "BAD_BYTE EQU -129
LD (IX+1),BAD_BYTE"; do
    printf '%s\n' "$bad" > "$TMP/z80-symbol-address-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-symbol-address-invalid.bin" "$TMP/z80-symbol-address-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects out-of-range EQU address/displacement"
        fail=$((fail + 1))
    else
        echo "PASS   z80 rejects out-of-range EQU address/displacement"
        pass=$((pass + 1))
    fi
done



# Exhaustive undocumented Z80 index-half coverage. Check all legal LD
# transfers between IXH/IXL or IYH/IYL and the ordinary 8-bit registers,
# plus all same-index half-register transfers. Cross-index and H/L mixing
# remain invalid and are checked separately below.
: > "$TMP/z80_index_half_matrix.asm"
expected=""
for pair in IX IY; do
    if [ "$pair" = IX ]; then prefix=dd; else prefix=fd; fi
    for dst in 4 5; do
        for src in 0 1 2 3 7 4 5; do
            case "$src" in
                0) s=B ;; 1) s=C ;; 2) s=D ;; 3) s=E ;;
                4) s=${pair}H ;; 5) s=${pair}L ;; 7) s=A ;;
            esac
            if [ "$dst" -eq 4 ]; then d=${pair}H; else d=${pair}L; fi
            printf 'LD %s,%s\n' "$d" "$s" >> "$TMP/z80_index_half_matrix.asm"
            expected="${expected}${prefix}$(printf '%02x' $((0x40 + dst * 8 + src)))"
        done
    done
    for dst in 0 1 2 3 7; do
        case "$dst" in
            0) d=B ;; 1) d=C ;; 2) d=D ;; 3) d=E ;; 7) d=A ;;
        esac
        for src in 4 5; do
            if [ "$src" -eq 4 ]; then s=${pair}H; else s=${pair}L; fi
            printf 'LD %s,%s\n' "$d" "$s" >> "$TMP/z80_index_half_matrix.asm"
            expected="${expected}${prefix}$(printf '%02x' $((0x40 + dst * 8 + src)))"
        done
    done
done
if "$ASM" -t z80 -o "$TMP/z80_index_half_matrix.bin" "$TMP/z80_index_half_matrix.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_index_half_matrix.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 exhaustive legal IXH/IXL/IYH/IYL LD matrix"
    pass=$((pass + 1))
else
    echo "FAIL   z80 exhaustive legal IXH/IXL/IYH/IYL LD matrix"
    fail=$((fail + 1))
fi

# Every index-half ALU source form, immediate load, and INC/DEC encoding.
: > "$TMP/z80_index_half_alu.asm"
expected=""
for pair in IX IY; do
    if [ "$pair" = IX ]; then prefix=dd; else prefix=fd; fi
    for mnemonic in ADD ADC SUB SBC AND XOR OR CP; do
        case "$mnemonic" in
            ADD) base=0x80 ;; ADC) base=0x88 ;; SUB) base=0x90 ;; SBC) base=0x98 ;;
            AND) base=0xa0 ;; XOR) base=0xa8 ;; OR) base=0xb0 ;; CP) base=0xb8 ;;
        esac
        for reg in 4 5; do
            if [ "$reg" -eq 4 ]; then source=${pair}H; else source=${pair}L; fi
            if [ "$mnemonic" = SUB ] || [ "$mnemonic" = AND ] ||
               [ "$mnemonic" = XOR ] || [ "$mnemonic" = OR ] ||
               [ "$mnemonic" = CP ]; then
                printf '%s %s\n' "$mnemonic" "$source" >> "$TMP/z80_index_half_alu.asm"
            else
                printf '%s A,%s\n' "$mnemonic" "$source" >> "$TMP/z80_index_half_alu.asm"
            fi
            expected="${expected}${prefix}$(printf '%02x' $((base + reg)))"
        done
    done
    for reg in 4 5; do
        if [ "$reg" -eq 4 ]; then
            half=${pair}H; loadop=0x26; incop=0x24; decop=0x25
        else
            half=${pair}L; loadop=0x2e; incop=0x2c; decop=0x2d
        fi
        printf 'LD %s,0x5a\nINC %s\nDEC %s\n' "$half" "$half" "$half" >> "$TMP/z80_index_half_alu.asm"
        expected="${expected}${prefix}$(printf '%02x' "$loadop")5a${prefix}$(printf '%02x' "$incop")${prefix}$(printf '%02x' "$decop")"
    done
done
if "$ASM" -t z80 -o "$TMP/z80_index_half_alu.bin" "$TMP/z80_index_half_alu.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_index_half_alu.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 complete IXH/IXL/IYH/IYL ALU, immediate, and INC/DEC matrix"
    pass=$((pass + 1))
else
    echo "FAIL   z80 complete IXH/IXL/IYH/IYL ALU, immediate, and INC/DEC matrix"
    fail=$((fail + 1))
fi

for bad in "LD IXH,IYH" "LD IXL,IYL" "LD IYH,IXL" "LD IYL,IXH" \
           "LD IXH,H" "LD H,IXH" "LD IXL,L" "LD L,IYL" \
           "LD IYH,H" "LD IYL,L"; do
    printf '%s\n' "$bad" > "$TMP/z80-index-half-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-index-half-invalid.bin" "$TMP/z80-index-half-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects illegal index-half register mixing: $bad"
        fail=$((fail + 1))
    else
        echo "PASS   z80 rejects illegal index-half register mixing"
        pass=$((pass + 1))
    fi
done


# DJNZ uses the same signed relative range as JR, but has its own encoder.
# Verify both limits, both out-of-range directions, and 16-bit PC wraparound.
{
    echo 'DJNZ target'
    i=0
    while [ "$i" -lt 127 ]; do echo 'dc.b 0'; i=$((i + 1)); done
    echo 'target: NOP'
} > "$TMP/z80-djnz-forward-limit.asm"
if "$ASM" -t z80 -o "$TMP/z80-djnz-forward-limit.bin" "$TMP/z80-djnz-forward-limit.asm" >/dev/null 2>&1 &&
   [ "$(od -An -tx1 -N2 "$TMP/z80-djnz-forward-limit.bin" | tr -d '[:space:]')" = "107f" ] &&
   [ "$(wc -c < "$TMP/z80-djnz-forward-limit.bin" | tr -d ' ')" = "130" ]; then
    echo "PASS   z80 DJNZ accepts +127 relative displacement"
    pass=$((pass + 1))
else
    echo "FAIL   z80 DJNZ accepts +127 relative displacement"
    fail=$((fail + 1))
fi

{
    echo 'target:'
    i=0
    while [ "$i" -lt 126 ]; do echo 'dc.b 0'; i=$((i + 1)); done
    echo 'DJNZ target'
} > "$TMP/z80-djnz-backward-limit.asm"
if "$ASM" -t z80 -o "$TMP/z80-djnz-backward-limit.bin" "$TMP/z80-djnz-backward-limit.asm" >/dev/null 2>&1 &&
   [ "$(od -An -tx1 -j126 -N2 "$TMP/z80-djnz-backward-limit.bin" | tr -d '[:space:]')" = "1080" ] &&
   [ "$(wc -c < "$TMP/z80-djnz-backward-limit.bin" | tr -d ' ')" = "128" ]; then
    echo "PASS   z80 DJNZ accepts -128 relative displacement"
    pass=$((pass + 1))
else
    echo "FAIL   z80 DJNZ accepts -128 relative displacement"
    fail=$((fail + 1))
fi

for direction in forward backward; do
    if [ "$direction" = forward ]; then
        {
            echo 'DJNZ target'
            i=0
            while [ "$i" -lt 128 ]; do echo 'dc.b 0'; i=$((i + 1)); done
            echo 'target: NOP'
        } > "$TMP/z80-djnz-out-of-range.asm"
    else
        {
            echo 'target:'
            i=0
            while [ "$i" -lt 127 ]; do echo 'dc.b 0'; i=$((i + 1)); done
            echo 'DJNZ target'
        } > "$TMP/z80-djnz-out-of-range.asm"
    fi
    if "$ASM" -t z80 -o "$TMP/z80-djnz-out-of-range.bin" "$TMP/z80-djnz-out-of-range.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 DJNZ rejects $direction displacement outside signed range"
        fail=$((fail + 1))
    else
        echo "PASS   z80 DJNZ rejects $direction displacement outside signed range"
        pass=$((pass + 1))
    fi
done

printf 'DJNZ $0000\n' > "$TMP/z80-djnz-wrap.asm"
if "$ASM" -t z80 --base 65534 -o "$TMP/z80-djnz-wrap.bin" "$TMP/z80-djnz-wrap.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80-djnz-wrap.bin" | tr -d '[:space:]')" = "1000" ]; then
    echo "PASS   z80 DJNZ relative branch wraps at 16-bit address boundary"
    pass=$((pass + 1))
else
    echo "FAIL   z80 DJNZ relative branch wraps at 16-bit address boundary"
    fail=$((fail + 1))
fi


# Z80 byte immediates accept signed -128 through unsigned 255, while
# 16-bit immediates accept signed -32768 through unsigned 65535.
: > "$TMP/z80_immediate_boundaries.asm"
expected=""
for reg in B C D E H L '(HL)' A; do
    case "$reg" in
        B) opcode=06 ;; C) opcode=0e ;; D) opcode=16 ;; E) opcode=1e ;;
        H) opcode=26 ;; L) opcode=2e ;; '(HL)') opcode=36 ;; A) opcode=3e ;;
    esac
    printf 'LD %s,-128\nLD %s,255\n' "$reg" "$reg" >> "$TMP/z80_immediate_boundaries.asm"
    expected="${expected}${opcode}80${opcode}ff"
done
for pair in BC DE HL SP IX IY; do
    case "$pair" in
        BC) prefix=""; opcode=01 ;;
        DE) prefix=""; opcode=11 ;;
        HL) prefix=""; opcode=21 ;;
        SP) prefix=""; opcode=31 ;;
        IX) prefix=dd; opcode=21 ;;
        IY) prefix=fd; opcode=21 ;;
    esac
    printf 'LD %s,-32768\nLD %s,65535\n' "$pair" "$pair" >> "$TMP/z80_immediate_boundaries.asm"
    expected="${expected}${prefix}${opcode}0080${prefix}${opcode}ffff"
done
for mnemonic in ADD ADC SUB SBC AND XOR OR CP; do
    case "$mnemonic" in
        ADD) opcode=c6 ;; ADC) opcode=ce ;; SUB) opcode=d6 ;; SBC) opcode=de ;;
        AND) opcode=e6 ;; XOR) opcode=ee ;; OR) opcode=f6 ;; CP) opcode=fe ;;
    esac
    if [ "$mnemonic" = SUB ] || [ "$mnemonic" = AND ] ||
       [ "$mnemonic" = XOR ] || [ "$mnemonic" = OR ] ||
       [ "$mnemonic" = CP ]; then
        printf '%s -128\n%s 255\n' "$mnemonic" "$mnemonic" >> "$TMP/z80_immediate_boundaries.asm"
    else
        printf '%s A,-128\n%s A,255\n' "$mnemonic" "$mnemonic" >> "$TMP/z80_immediate_boundaries.asm"
    fi
    expected="${expected}${opcode}80${opcode}ff"
done
if "$ASM" -t z80 -o "$TMP/z80_immediate_boundaries.bin" "$TMP/z80_immediate_boundaries.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_immediate_boundaries.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 signed/unsigned byte and 16-bit immediate boundaries"
    pass=$((pass + 1))
else
    echo "FAIL   z80 signed/unsigned byte and 16-bit immediate boundaries"
    fail=$((fail + 1))
fi

for bad in \
    "LD B,-129" "LD C,256" "LD (HL),-129" "LD A,256" \
    "LD IXH,-129" "LD IYL,256" \
    "ADD A,-129" "ADC A,256" "SUB -129" "SBC A,256" \
    "AND -129" "XOR 256" "OR -129" "CP 256" \
    "LD BC,-32769" "LD DE,65536" "LD HL,-32769" \
    "LD SP,65536" "LD IX,-32769" "LD IY,65536"; do
    printf '%s\n' "$bad" > "$TMP/z80-immediate-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-immediate-invalid.bin" "$TMP/z80-immediate-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects out-of-range immediate: $bad"
        fail=$((fail + 1))
    else
        echo "PASS   z80 rejects out-of-range immediate"
        pass=$((pass + 1))
    fi
done

# Symbolic IX/IY and SP-relative displacements must retain signed 8-bit
# boundaries after pass-two EQU resolution, just like literal displacements.
cat > "$TMP/z80_symbolic_displacement_boundaries.asm" <<'EOF'
NEG128 EQU 128
POS127 EQU 127
LD A,(IX-NEG128)
LD (IX+POS127),A
LD B,(IY-NEG128)
LD (IY+POS127),C
BIT 7,(IX-NEG128)
RES 0,(IY+POS127)
LD HL,SP-NEG128
LD HL,SP+POS127
EOF
expected="dd7e80dd777ffd4680fd717fddcb807efdcb7f86f880f87f"
expected=$(printf '%s' "$expected" | tr -d ' ')
if "$ASM" -t z80 -o "$TMP/z80_symbolic_displacement_boundaries.bin" "$TMP/z80_symbolic_displacement_boundaries.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_symbolic_displacement_boundaries.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 symbolic IX/IY and SP displacement boundaries"
    pass=$((pass + 1))
else
    echo "FAIL   z80 symbolic IX/IY and SP displacement boundaries"
    fail=$((fail + 1))
fi

for bad in \
    "TOO_POS EQU 128
LD A,(IX+TOO_POS)" \
    "TOO_NEG EQU 129
LD A,(IY-TOO_NEG)" \
    "TOO_POS EQU 128
LD HL,SP+TOO_POS" \
    "TOO_NEG EQU 129
LD HL,SP-TOO_NEG"; do
    printf '%s\n' "$bad" > "$TMP/z80-symbolic-displacement-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-symbolic-displacement-invalid.bin" "$TMP/z80-symbolic-displacement-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects out-of-range symbolic displacement"
        fail=$((fail + 1))
    else
        echo "PASS   z80 rejects out-of-range symbolic displacement"
        pass=$((pass + 1))
    fi
done

# Exercise every ALU operation against IX/IY indexed memory at both
# signed displacement endpoints, plus indexed INC/DEC and range rejection.
: > "$TMP/z80_indexed_alu_boundaries.asm"
expected=""
for index in IX IY; do
    case "$index" in IX) prefix=dd ;; IY) prefix=fd ;; esac
    for displacement in -128 127; do
        case "$displacement" in -128) disp=80 ;; 127) disp=7f ;; esac
        for mnemonic in ADD ADC SUB SBC AND XOR OR CP; do
            case "$mnemonic" in
                ADD) opcode=86; syntax="ADD A,(%s%s%d)" ;;
                ADC) opcode=8e; syntax="ADC A,(%s%s%d)" ;;
                SUB) opcode=96; syntax="SUB (%s%s%d)" ;;
                SBC) opcode=9e; syntax="SBC A,(%s%s%d)" ;;
                AND) opcode=a6; syntax="AND (%s%s%d)" ;;
                XOR) opcode=ae; syntax="XOR (%s%s%d)" ;;
                OR)  opcode=b6; syntax="OR (%s%s%d)" ;;
                CP)  opcode=be; syntax="CP (%s%s%d)" ;;
            esac
            if [ "$displacement" -lt 0 ]; then sign=-; magnitude=$(( -displacement )); else sign=+; magnitude=$displacement; fi
            printf "$syntax\n" "$index" "$sign" "$magnitude" >> "$TMP/z80_indexed_alu_boundaries.asm"
            expected="${expected}${prefix}${opcode}${disp}"
        done
    done
    for displacement in -128 127; do
        if [ "$displacement" -lt 0 ]; then disp=80; else disp=7f; fi
        if [ "$displacement" -lt 0 ]; then sign=-; magnitude=$(( -displacement )); else sign=+; magnitude=$displacement; fi
        printf 'INC (%s%s%d)\nDEC (%s%s%d)\n' "$index" "$sign" "$magnitude" "$index" "$sign" "$magnitude" >> "$TMP/z80_indexed_alu_boundaries.asm"
        expected="${expected}${prefix}34${disp}${prefix}35${disp}"
    done
done
if "$ASM" -t z80 -o "$TMP/z80_indexed_alu_boundaries.bin" "$TMP/z80_indexed_alu_boundaries.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_indexed_alu_boundaries.bin" | tr -d '[:space:]')" = "$expected" ]; then
    echo "PASS   z80 complete IX/IY indexed ALU and INC/DEC displacement matrix"
    pass=$((pass + 1))
else
    echo "FAIL   z80 complete IX/IY indexed ALU and INC/DEC displacement matrix"
    fail=$((fail + 1))
fi

for bad in \
    "ADD A,(IX+128)" "ADC A,(IY-129)" "SUB (IX+128)" "SBC A,(IY-129)" \
    "AND (IX+128)" "XOR (IY-129)" "OR (IX+128)" "CP (IY-129)" \
    "INC (IX+128)" "DEC (IY-129)"; do
    printf '%s\n' "$bad" > "$TMP/z80-indexed-alu-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-indexed-alu-invalid.bin" "$TMP/z80-indexed-alu-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects out-of-range indexed ALU/INC/DEC displacement"
        fail=$((fail + 1))
    else
        echo "PASS   z80 rejects out-of-range indexed ALU/INC/DEC displacement"
        pass=$((pass + 1))
    fi
done

echo "$pass ok, $fail failed"
[ "$fail" -eq 0 ]


# Literal Z80 LD HL,SP+e displacement limits and operand validation.
# Check both signed endpoints, zero in either spelling, and hexadecimal input.
cat > "$TMP/z80_sp_relative_literals.asm" <<'EOF'
LD HL,SP-128
LD HL,SP+127
LD HL,SP+0
LD HL,SP-0
LD HL,SP-$80
LD HL,SP+$7f
EOF
if "$ASM" -t z80 -o "$TMP/z80_sp_relative_literals.bin" "$TMP/z80_sp_relative_literals.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/z80_sp_relative_literals.bin" | tr -d '[:space:]')" = "f880f87ff800f800f880f87f" ]; then
    echo "PASS   z80 LD HL,SP+e literal signed displacement boundaries"
    pass=$((pass + 1))
else
    echo "FAIL   z80 LD HL,SP+e literal signed displacement boundaries"
    fail=$((fail + 1))
fi

for bad in "LD HL,SP+128" "LD HL,SP-129" "LD HL,SP+$80" "LD HL,SP-$81" \\
           "LD DE,SP+1" "LD IX,SP+1" "LD SP,SP+1"; do
    printf '%s\\n' "$bad" > "$TMP/z80-sp-relative-invalid.asm"
    if "$ASM" -t z80 -o "$TMP/z80-sp-relative-invalid.bin" "$TMP/z80-sp-relative-invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects invalid SP-relative LD form: $bad"
        fail=$((fail + 1))
    else
        echo "PASS   z80 rejects invalid SP-relative LD form"
        pass=$((pass + 1))
    fi
done

echo "$pass ok, $fail failed"
[ "$fail" -eq 0 ]
