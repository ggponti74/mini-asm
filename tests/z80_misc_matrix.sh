#!/bin/sh
# Exhaustive documented Z80 INC/DEC, rotate/shift, and bit-operation matrices.
ASM=${1:-./mini-asm}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/mini-asm-z80-misc.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT
pass=0
fail=0

append_byte() {
    printf '%02x' "$1" >> "$TMP/expected"
}

check_bytes() {
    name=$1
    source=$2
    : > "$TMP/expected"
    : > "$TMP/actual"
    if ! "$ASM" -t z80 -o "$TMP/test.bin" "$source" >/dev/null 2>&1; then
        echo "FAIL   $name (assembly rejected)"
        fail=$((fail + 1))
        return
    fi
    od -An -v -tx1 "$TMP/test.bin" | tr -d '[:space:]' > "$TMP/actual"
    if [ "$(cat "$TMP/actual")" = "$(cat "$TMP/expected")" ]; then
        pass=$((pass + 1))
    else
        echo "FAIL   $name (unexpected bytes)"
        fail=$((fail + 1))
    fi
}

# INC/DEC: all 8-bit registers, (HL), all 16-bit pairs, IX and IY.
: > "$TMP/inc-dec.asm"
: > "$TMP/expected"
for spec in "B 0" "C 1" "D 2" "E 3" "H 4" "L 5" "(HL) 6" "A 7"; do
    set -- $spec
    printf 'INC %s\nDEC %s\n' "$1" "$1" >> "$TMP/inc-dec.asm"
    append_byte $((0x04 + $2 * 8))
    append_byte $((0x05 + $2 * 8))
done
for spec in "BC 0" "DE 1" "HL 2" "SP 3"; do
    set -- $spec
    printf 'INC %s\nDEC %s\n' "$1" "$1" >> "$TMP/inc-dec.asm"
    append_byte $((0x03 + $2 * 0x10))
    append_byte $((0x0B + $2 * 0x10))
done
cat >> "$TMP/inc-dec.asm" <<'EOF'
INC IX
DEC IX
INC IY
DEC IY
EOF
append_byte 0xDD
append_byte 0x23
append_byte 0xDD
append_byte 0x2B
append_byte 0xFD
append_byte 0x23
append_byte 0xFD
append_byte 0x2B
check_bytes "documented Z80 INC/DEC register and pair matrix" "$TMP/inc-dec.asm"

# All documented CB rotate/shift operations on registers and (HL).
: > "$TMP/rotate.asm"
: > "$TMP/expected"
for spec in "B 0" "C 1" "D 2" "E 3" "H 4" "L 5" "(HL) 6" "A 7"; do
    set -- $spec
    for form in "RLC 0x00" "RRC 0x08" "RL 0x10" "RR 0x18" "SLA 0x20" "SRA 0x28" "SRL 0x38"; do
        set -- $form
        mnemonic=$1
        base=$2
        printf '%s %s\n' "$mnemonic" "$spec" >> "$TMP/rotate.asm"
        append_byte 0xCB
        append_byte $((base + $(printf '%d' "$2") ))
    done
done
check_bytes "documented Z80 CB rotate/shift register matrix" "$TMP/rotate.asm"

# Indexed rotate/shift uses DD/FD CB displacement opcode; undocumented
# result-register variants are deliberately excluded.
: > "$TMP/indexed-rotate.asm"
: > "$TMP/expected"
for form in "RLC 0x00" "RRC 0x08" "RL 0x10" "RR 0x18" "SLA 0x20" "SRA 0x28" "SRL 0x38"; do
    set -- $form
    mnemonic=$1
    base=$2
    for index in "IX DD 5" "IY FD -5"; do
        set -- $index
        printf '%s (%s%+d)\n' "$mnemonic" "$1" "$3" >> "$TMP/indexed-rotate.asm"
        append_byte $((0x$2))
        append_byte 0xCB
        append_byte $(( $3 < 0 ? 256 + $3 : $3 ))
        append_byte $(( $(printf '%d' "$base") + 6 ))
    done
done
check_bytes "documented Z80 indexed rotate/shift matrix" "$TMP/indexed-rotate.asm"

# BIT/RES/SET for every bit number and every 8-bit register/(HL) target.
: > "$TMP/bit.asm"
: > "$TMP/expected"
for form in "BIT 0x40" "RES 0x80" "SET 0xC0"; do
    set -- $form
    mnemonic=$1
    base=$2
    for bit in 0 1 2 3 4 5 6 7; do
        for spec in "B 0" "C 1" "D 2" "E 3" "H 4" "L 5" "(HL) 6" "A 7"; do
            set -- $spec
            printf '%s %s,%s\n' "$mnemonic" "$bit" "$1" >> "$TMP/bit.asm"
            append_byte 0xCB
            append_byte $(( $(printf '%d' "$base") + bit * 8 + $2 ))
        done
    done
done
check_bytes "documented Z80 BIT/RES/SET register matrix" "$TMP/bit.asm"

# Indexed BIT/RES/SET uses DD/FD CB displacement opcode; result-register
# variants are undocumented and are not part of this matrix.
: > "$TMP/indexed-bit.asm"
: > "$TMP/expected"
for form in "BIT 0x40" "RES 0x80" "SET 0xC0"; do
    set -- $form
    mnemonic=$1
    base=$2
    for bit in 0 1 2 3 4 5 6 7; do
        printf '%s %s,(IX+5)\n' "$mnemonic" "$bit" >> "$TMP/indexed-bit.asm"
        append_byte 0xDD
        append_byte 0xCB
        append_byte 0x05
        append_byte $(( $(printf '%d' "$base") + bit * 8 + 6 ))
        printf '%s %s,(IY-5)\n' "$mnemonic" "$bit" >> "$TMP/indexed-bit.asm"
        append_byte 0xFD
        append_byte 0xCB
        append_byte 0xFB
        append_byte $(( $(printf '%d' "$base") + bit * 8 + 6 ))
    done
done
check_bytes "documented Z80 indexed BIT/RES/SET matrix" "$TMP/indexed-bit.asm"

# Reject invalid pair targets, out-of-range displacements/bits, undocumented
# SLL and undocumented indexed result-register forms.
for bad in \
    "INC AF" "DEC AF" "INC (IX+128)" "DEC (IY-129)" \
    "BIT -1,A" "BIT 8,A" "RES -1,B" "SET 8,(HL)" "SET 0,F" \
    "SLL B" "RLC (IX+1),B" "RLC (HL),B" "RES 0,(IX+1),B"; do
    printf '%s\n' "$bad" > "$TMP/invalid.asm"
    if "$ASM" -t z80 -o "$TMP/invalid.bin" "$TMP/invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects invalid/undocumented form: $bad"
        fail=$((fail + 1))
    else
        pass=$((pass + 1))
    fi
done

echo "$pass ok, $fail failed"
[ "$fail" -eq 0 ]
