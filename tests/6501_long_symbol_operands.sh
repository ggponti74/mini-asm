#!/bin/sh
# Maximum-length symbols must survive 6501 indirect operand parsing.
ASM=${1:-./mini-asm}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/mini-asm-6501-long-symbols.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT
pass=0
fail=0

for length in 30 60 61 63; do
    symbol=$(printf '%*s' "$length" '' | tr ' ' a)
    cat > "$TMP/long-symbol.asm" <<EOF
$symbol EQU 16
LDA ($symbol,X)
LDA ($symbol),Y
EOF
    if "$ASM" -t 6501 -o "$TMP/long-symbol.bin" "$TMP/long-symbol.asm" >/dev/null 2>&1 &&
       [ "$(od -An -v -tx1 "$TMP/long-symbol.bin" | tr -d '[:space:]')" = "a110b110" ]; then
        pass=$((pass + 1))
    else
        echo "FAIL   6501 indirect operands with $length-character symbols"
        fail=$((fail + 1))
    fi
done

echo "$pass ok, $fail failed"
[ "$fail" -eq 0 ]
