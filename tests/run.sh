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
printf 'LDI A\\n' > "$TMP/z80_bad_block_operand.asm"
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
expected="ddcb0106ddcbfe5efdcb04aefdcb00fefdcbff2edd2612dd2e34fd2656fd2e78dd7cfd6fdd65fd60dd24dd2dfd24fd2ddd84dd8dfd94fd9ddda4ddadfdb4fdbd"
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
echo "$pass ok, $fail failed"
[ "$fail" -eq 0 ]