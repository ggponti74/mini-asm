#!/bin/sh
# run.sh - mini-asm regression tests (Linux: assembles to ELF and runs the result).
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
    "$TMP/prog"; report "$1" $?
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
    {
        echo "start:"
        echo "        include \"$sec\""
        echo "        moveq   #0, d0"
        echo "        rts"
        echo "        include \"$DIR/common.asm\""
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
if command -v qemu-arm >/dev/null 2>&1; then
    run_arm_program main "$DIR/main.asm"
    for sec in "$DIR"/sections/*.asm; do
        name=$(basename "$sec" .asm)
        {
            echo "start:"
            echo "        include \"$sec\""
            echo "        moveq   #0, d0"
            echo "        rts"
            echo "        include \"$DIR/common.asm\""
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
    echo "SKIP   arm/*   (qemu-arm not installed)"
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
    (cd "$TMP/work" && "$TMP/trap.elf" >/dev/null); report "$tname" $?
    if command -v qemu-arm >/dev/null 2>&1; then
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
        got=$(od -An -tx1 "$TMP/6501.bin" | tr -d ' \n')
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
expect_6501_failure 'STA #$01'
expect_6501_failure 'LDA ($100,X)'
expect_6501_failure 'LDA ($20,Y)'
expect_6501_failure 'JMP ($20,X)'
expect_6501_failure 'LDA $10000'
expect_6501_failure 'ASL D0'
expect_6501_failure 'BNE -1'
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

echo "----"
echo "$pass ok, $fail failed"
[ "$fail" -eq 0 ]