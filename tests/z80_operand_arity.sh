#!/bin/sh
# Reject malformed operand counts and unsupported operand combinations for
# documented Z80 instruction families.
ASM=${1:-./mini-asm}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/mini-asm-z80-arity.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT
pass=0
fail=0

for bad in \
    "NOP A" "HALT 0" "DI A" "EI 1" "EXX A" \
    "NEG A" "RETN A" "RETI A" "RRD A" "RLD A" \
    "LDI A" "LDIR A" "LDD A" "LDDR A" \
    "CPI A" "CPIR A" "CPD A" "CPDR A" \
    "INI A" "INIR A" "IND A" "INDR A" \
    "OUTI A" "OTIR A" "OUTD A" "OTDR A" \
    "RLCA A" "RRCA A" "RLA A" "RRA A" "CPL A" "DAA A" "SCF A" "CCF A" \
    "IM" "IM 1,2" \
    "RET NZ,Z" "JR" "JR NZ,Z,0x1234" "DJNZ" "DJNZ 1,2" \
    "JP" "JP NZ,0x1234,A" "CALL" "CALL NZ,0x1234,A" \
    "RST" "RST 8,A" \
    "PUSH" "PUSH BC,DE" "POP" "POP HL,IX" \
    "ADD A" "ADD A,B,C" "ADC A" "ADC A,B,C" \
    "SUB" "SUB B,C" "SBC A" "SBC A,B,C" \
    "AND" "AND B,C" "XOR" "XOR C,D" "OR" "OR A,B" "CP" "CP A,B" \
    "INC" "INC B,C" "DEC" "DEC (HL),A" \
    "BIT" "BIT 3" "BIT 3,A,B" \
    "RES" "RES 2" "RES 2,A,B" "SET" "SET 4" "SET 4,(HL),A" \
    "RLC" "RLC B,A" "SRA (HL),B" \
    "IN" "IN A" "IN A,(C),B" \
    "OUT" "OUT (C)" "OUT (C),A,B" \
    "LD A" "LD A,B,C" "EX" "EX DE,HL,AF"; do
    printf '%s\n' "$bad" > "$TMP/invalid.asm"
    if "$ASM" -t z80 -o "$TMP/invalid.bin" "$TMP/invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects malformed operand form: $bad"
        fail=$((fail + 1))
    else
        pass=$((pass + 1))
    fi
done

echo "$pass ok, $fail failed"
[ "$fail" -eq 0 ]
