#!/bin/sh
# Regression coverage for long symbols inside Z80 indirect memory operands.
ASM=${1:-./mini-asm}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/mini-asm-z80-long-symbols.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT
pass=0
fail=0

for length in 30 60; do
    address_label=$(printf '%*s' "$length" '' | tr ' ' a)
    displacement_label=$(printf '%*s' "$length" '' | tr ' ' b)
    cat > "$TMP/long-symbols.asm" <<EOF
$address_label EQU 4660
$displacement_label EQU 5
LD A,($address_label)
LD ($address_label),A
LD B,(IX+$displacement_label)
LD C,(IY-$displacement_label)
EOF
    expected="3a3412323412dd4605fd4efb"
    if "$ASM" -t z80 -o "$TMP/long-symbols.bin" "$TMP/long-symbols.asm" >/dev/null 2>&1 &&
       [ "$(od -An -v -tx1 "$TMP/long-symbols.bin" | tr -d '[:space:]')" = "$expected" ]; then
        pass=$((pass + 1))
    else
        echo "FAIL   z80 indirect memory operands with $length-character symbols"
        fail=$((fail + 1))
    fi
done

echo "$pass ok, $fail failed"
[ "$fail" -eq 0 ]
