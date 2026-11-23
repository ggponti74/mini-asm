#!/bin/sh
# Exhaustive negative-mode checks for the currently implemented 6501 opcode
# families. Positive opcode/addressing matrices live in tests/run.sh.
ASM=${1:-./mini-asm}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/mini-asm-6501-invalid.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT
pass=0
fail=0

expect_failure() {
    source=$1
    printf '%s\n' "$source" > "$TMP/invalid.asm"
    if "$ASM" -t 6501 -o "$TMP/invalid.bin" "$TMP/invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   6501 unexpectedly accepts: $source"
        fail=$((fail + 1))
    else
        pass=$((pass + 1))
    fi
}

# These instructions have no immediate encoding.
for mnemonic in STA STX STY BIT INC DEC ASL LSR ROL ROR JMP JSR; do
    expect_failure "$mnemonic #$12"
done

# Only the accumulator load/store and ALU families support the two
# zero-page-indirect forms. All other memory families must reject them.
for mnemonic in LDX LDY STX STY BIT CPX CPY INC DEC ASL LSR ROL ROR; do
    expect_failure "$mnemonic ($12,X)"
    expect_failure "$mnemonic ($12),Y"
done
expect_failure 'JMP ($12,X)'
expect_failure 'JMP ($12),Y'
expect_failure 'JSR ($1234)'
expect_failure 'JSR ($12,X)'

# Index direction is opcode-specific. These forms have no matching opcode.
for source in \
    'LDX $12,X' 'LDY $12,Y' \
    'STX $12,X' 'STY $12,Y' \
    'BIT $12,X' 'BIT $12,Y' \
    'CPX $12,X' 'CPX $12,Y' 'CPY $12,X' 'CPY $12,Y' \
    'INC $12,Y' 'DEC $12,Y' \
    'ASL $12,Y' 'LSR $12,Y' 'ROL $12,Y' 'ROR $12,Y' \
    'JMP $1234,X' 'JMP $1234,Y' 'JSR $1234,X' 'JSR $1234,Y' \
    'LDA $12,Z' 'STA $12,Z' 'ADC $12,Z'; do
    expect_failure "$source"
done

# Branch operands are absolute destinations, never immediate, indexed, or
# parenthesized memory operands. Check every implemented branch mnemonic.
for mnemonic in BRA BCC BCS BEQ BMI BNE BPL BVC BVS; do
    expect_failure "$mnemonic #$12"
    expect_failure "$mnemonic $12,X"
    expect_failure "$mnemonic $12,Y"
    expect_failure "$mnemonic ($12,X)"
    expect_failure "$mnemonic ($12),Y"
done

# Reject malformed combinations that can otherwise resemble valid
# parenthesized or indexed syntax.
for source in \
    'LDA ($12)' 'LDA ($12,Y)' 'LDA ($12),X' 'LDA ($12,X),Y' \
    'STA ($12)' 'STA ($12,Y)' 'STA ($12),X' 'STA ($12,X),Y' \
    'ADC ($12)' 'ADC ($12,Y)' 'ADC ($12),X' 'ADC ($12,X),Y' \
    'LDA $12,X,Y' 'STA $12,X,Y' 'ADC $12,X,Y' \
    'JMP ($12),X' 'JMP ($12),Y'; do
    expect_failure "$source"
done

echo "$pass ok, $fail failed"
[ "$fail" -eq 0 ]
