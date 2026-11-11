#!/bin/sh
# Regression coverage for long symbols inside Z80 indirect memory operands.
ASM=${1:-./mini-asm}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/mini-asm-z80-long-symbols.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT
pass=0
fail=0

for length in 30 60 61 63; do
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


# Label definitions must obey the same 63-character limit as label operands.
label63=$(printf '%*s' 63 '' | tr ' ' c)
label64=$(printf '%*s' 64 '' | tr ' ' d)
for style in colon colonless; do
    if [ "$style" = colon ]; then
        printf '%s: NOP\\nJP %s\\n' "$label63" "$label63" > "$TMP/label-boundary.asm"
    else
        printf '%s NOP\\nJP %s\\n' "$label63" "$label63" > "$TMP/label-boundary.asm"
    fi
    if "$ASM" -t z80 -o "$TMP/label-boundary.bin" "$TMP/label-boundary.asm" >/dev/null 2>&1 &&
       [ "$(od -An -v -tx1 "$TMP/label-boundary.bin" | tr -d '[:space:]')" = "00c30000" ]; then
        pass=$((pass + 1))
    else
        echo "FAIL   z80 accepts 63-character $style label"
        fail=$((fail + 1))
    fi
done

for style in colon colonless; do
    if [ "$style" = colon ]; then
        printf '%s: NOP\\n' "$label64" > "$TMP/label-too-long.asm"
    else
        printf '%s NOP\\n' "$label64" > "$TMP/label-too-long.asm"
    fi
    if "$ASM" -t z80 -o "$TMP/label-too-long.bin" "$TMP/label-too-long.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects 64-character $style label"
        fail=$((fail + 1))
    else
        pass=$((pass + 1))
    fi
done

echo "$pass ok, $fail failed"
[ "$fail" -eq 0 ]
