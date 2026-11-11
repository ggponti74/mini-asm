#!/bin/sh
# Physical source lines must never be split into separate assembler statements.
ASM=${1:-./mini-asm}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/mini-asm-line-length.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT
pass=0
fail=0

check_boundary() {
    target=$1
    expected=$2
    suffix=$3
    file="$TMP/boundary-$target-$suffix.asm"
    output="$TMP/boundary-$target-$suffix.bin"

    printf 'NOP' > "$file"
    printf '%1020s' '' >> "$file"
    if [ "$suffix" = newline ]; then
        printf '\n' >> "$file"
    fi

    if "$ASM" -t "$target" -o "$output" "$file" >/dev/null 2>&1 &&
       [ "$(od -An -v -tx1 "$output" | tr -d '[:space:]')" = "$expected" ]; then
        pass=$((pass + 1))
    else
        echo "FAIL   $target accepts a source line of exactly 1023 characters ($suffix)"
        fail=$((fail + 1))
    fi
}

check_overlong() {
    target=$1
    second_instruction=$2
    suffix=$3
    file="$TMP/overlong-$target-$suffix.asm"
    output="$TMP/overlong-$target-$suffix.bin"
    log="$TMP/overlong-$target-$suffix.log"

    printf 'NOP' > "$file"
    printf '%1020s' '' >> "$file"
    printf '%s' "$second_instruction" >> "$file"
    if [ "$suffix" = newline ]; then
        printf '\n' >> "$file"
    fi

    if "$ASM" -t "$target" -o "$output" "$file" > /dev/null 2>"$log"; then
        echo "FAIL   $target rejects an overlong physical source line ($suffix)"
        fail=$((fail + 1))
    elif grep -q 'source line exceeds 1023 characters' "$log"; then
        pass=$((pass + 1))
    else
        echo "FAIL   $target reports the overlong source line clearly ($suffix)"
        fail=$((fail + 1))
    fi
}

check_include() {
    target=$1
    second_instruction=$2
    child="$TMP/included-overlong-$target.asm"
    parent="$TMP/include-parent-$target.asm"
    log="$TMP/include-$target.log"

    {
        printf 'NOP'
        printf '%1020s' ''
        printf '%s\n' "$second_instruction"
    } > "$child"
    printf 'INCLUDE "%s"\n' "$child" > "$parent"

    if "$ASM" -t "$target" -o "$TMP/include-$target.bin" "$parent" > /dev/null 2>"$log"; then
        echo "FAIL   $target rejects an overlong line in an included source file"
        fail=$((fail + 1))
    elif grep -q 'source line exceeds 1023 characters' "$log"; then
        pass=$((pass + 1))
    else
        echo "FAIL   $target reports an overlong included source line clearly"
        fail=$((fail + 1))
    fi
}

# A 1023-character line fits, whether or not it ends with a newline.
check_boundary z80 00 newline
check_boundary z80 00 eof
check_boundary 6501 ea newline
check_boundary 6501 ea eof

# Previously, fgets() split these into NOP plus a second valid instruction.
check_overlong z80 RET newline
check_overlong z80 RET eof
check_overlong 6501 RTS newline
check_overlong 6501 RTS eof

# Recursive source processing must apply the same physical-line rule.
check_include z80 RET
check_include 6501 RTS

echo "$pass ok, $fail failed"
[ "$fail" -eq 0 ]
