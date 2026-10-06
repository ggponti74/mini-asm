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
# 4. tests/main.asm must also assemble for the x86 (PE) target (assembly only).

ASM=${1:-./mini-asm}
DIR=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d "${TMPDIR:-/tmp}/mini-asm-test.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT

pass=0; fail=0

# "main" includes every section, so it is expected to fail while any entry is listed.
xfail_listed() {
    if [ "$1" = main ]; then grep -qv '^#' "$DIR/xfail.txt" 2>/dev/null
    else grep -qx "$1" "$DIR/xfail.txt" 2>/dev/null; fi
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

# 3. ARM programs (tests/arm/*.asm, first line "; EXIT: n"): run with qemu-arm if it is installed
if command -v qemu-arm >/dev/null 2>&1; then
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

# 4. other x86 output format: assembly only (a PE file cannot run here)
if "$ASM" -t x86 -o "$TMP/main.exe" "$DIR/main.asm" >/dev/null 2>&1; then
    echo "PASS   main (x86/PE assembles)"; pass=$((pass + 1))
else
    echo "FAIL   main (x86/PE does not assemble)"; fail=$((fail + 1))
fi

echo "----"
echo "$pass ok, $fail failed"
[ "$fail" -eq 0 ]
