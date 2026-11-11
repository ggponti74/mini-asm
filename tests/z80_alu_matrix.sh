#!/bin/sh
# Exhaustive documented Z80 arithmetic/logical opcode matrix.
ASM=${1:-./mini-asm}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/mini-asm-z80-alu.XXXXXX") || exit 2
trap 'rm -rf "$TMP"' EXIT
pass=0
fail=0

# Every documented 8-bit ALU operation against all seven register sources
# plus (HL). ADD/ADC/SBC retain their explicit A destination syntax.
: > "$TMP/registers.asm"
expected=""
for mnemonic in ADD ADC SUB SBC AND XOR OR CP; do
    case "$mnemonic" in
        ADD) base=80; syntax='ADD A,%s' ;;
        ADC) base=88; syntax='ADC A,%s' ;;
        SUB) base=90; syntax='SUB %s' ;;
        SBC) base=98; syntax='SBC A,%s' ;;
        AND) base=a0; syntax='AND %s' ;;
        XOR) base=a8; syntax='XOR %s' ;;
        OR)  base=b0; syntax='OR %s' ;;
        CP)  base=b8; syntax='CP %s' ;;
    esac
    r=0
    for reg in B C D E H L '(HL)' A; do
        printf "$syntax\n" "$reg" >> "$TMP/registers.asm"
        expected="${expected}$(printf '%02x' "$((0x$base + r))")"
        r=$((r + 1))
    done
done
if "$ASM" -t z80 -o "$TMP/registers.bin" "$TMP/registers.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/registers.bin" | tr -d '[:space:]')" = "$expected" ]; then
    pass=$((pass + 1))
else
    echo "FAIL   z80 complete documented 8-bit ALU register-source matrix"
    fail=$((fail + 1))
fi

# All legal 16-bit ADD/ADC/SBC HL,pair combinations.
: > "$TMP/pairs.asm"
expected=""
for mnemonic in ADD ADC SBC; do
    for pair in BC DE HL SP; do
        case "$pair" in BC) p=0 ;; DE) p=1 ;; HL) p=2 ;; SP) p=3 ;; esac
        if [ "$mnemonic" = ADD ]; then
            printf 'ADD HL,%s\n' "$pair" >> "$TMP/pairs.asm"
            expected="${expected}$(printf '%02x' "$((0x09 + p * 0x10))")"
        else
            if [ "$mnemonic" = ADC ]; then opcode=$((0x4a + p * 0x10)); else opcode=$((0x42 + p * 0x10)); fi
            printf '%s HL,%s\n' "$mnemonic" "$pair" >> "$TMP/pairs.asm"
            expected="${expected}ed$(printf '%02x' "$opcode")"
        fi
    done
done
if "$ASM" -t z80 -o "$TMP/pairs.bin" "$TMP/pairs.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/pairs.bin" | tr -d '[:space:]')" = "$expected" ]; then
    pass=$((pass + 1))
else
    echo "FAIL   z80 documented 16-bit ADD/ADC/SBC HL,pair matrix"
    fail=$((fail + 1))
fi

# IX/IY support ADD with BC, DE, itself, and SP only.
: > "$TMP/index-pairs.asm"
expected=""
for index in IX IY; do
    if [ "$index" = IX ]; then prefix=dd; else prefix=fd; fi
    for pair in BC DE "$index" SP; do
        case "$pair" in BC) p=0 ;; DE) p=1 ;; IX|IY) p=2 ;; SP) p=3 ;; esac
        printf 'ADD %s,%s\n' "$index" "$pair" >> "$TMP/index-pairs.asm"
        expected="${expected}${prefix}$(printf '%02x' "$((0x09 + p * 0x10))")"
    done
done
if "$ASM" -t z80 -o "$TMP/index-pairs.bin" "$TMP/index-pairs.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/index-pairs.bin" | tr -d '[:space:]')" = "$expected" ]; then
    pass=$((pass + 1))
else
    echo "FAIL   z80 documented ADD IX/IY,pair matrix"
    fail=$((fail + 1))
fi

# INC/DEC for every 8-bit register, 16-bit pair, and index pair.
: > "$TMP/inc-dec.asm"
expected=""
for mnemonic in INC DEC; do
    if [ "$mnemonic" = INC ]; then rbase=04; pbase=03; ibase=23
    else rbase=05; pbase=0b; ibase=2b; fi
    r=0
    for reg in B C D E H L '(HL)' A; do
        printf '%s %s\n' "$mnemonic" "$reg" >> "$TMP/inc-dec.asm"
        expected="${expected}$(printf '%02x' "$((0x$rbase + r * 8))")"
        r=$((r + 1))
    done
    p=0
    for pair in BC DE HL SP; do
        printf '%s %s\n' "$mnemonic" "$pair" >> "$TMP/inc-dec.asm"
        expected="${expected}$(printf '%02x' "$((0x$pbase + p * 0x10))")"
        p=$((p + 1))
    done
    for index in IX IY; do
        if [ "$index" = IX ]; then prefix=dd; else prefix=fd; fi
        printf '%s %s\n' "$mnemonic" "$index" >> "$TMP/inc-dec.asm"
        expected="${expected}${prefix}${ibase}"
    done
done
if "$ASM" -t z80 -o "$TMP/inc-dec.bin" "$TMP/inc-dec.asm" >/dev/null 2>&1 &&
   [ "$(od -An -v -tx1 "$TMP/inc-dec.bin" | tr -d '[:space:]')" = "$expected" ]; then
    pass=$((pass + 1))
else
    echo "FAIL   z80 complete documented INC/DEC register and pair matrix"
    fail=$((fail + 1))
fi

# Invalid operand direction, register classes, and mismatched index pairs.
for bad in \
    "ADD B,C" "ADD A,BC" "ADD HL,A" "ADD HL,AF" \
    "ADC A,BC" "ADC BC,DE" "ADC IX,BC" "SBC A,HL" "SBC IY,DE" \
    "SUB A,B" "AND A,B" "XOR A,1" "OR HL,BC" "CP IX" \
    "INC AF" "DEC AF" "INC F" "DEC I" \
    "ADD IX,IY" "ADD IY,IX" "ADD IX,AF" "ADD IY,AF" \
    "ADD IX,IXH" "ADD IY,IYL" "ADC IX,BC" "SBC IY,SP"; do
    printf '%s\n' "$bad" > "$TMP/invalid.asm"
    if "$ASM" -t z80 -o "$TMP/invalid.bin" "$TMP/invalid.asm" >/dev/null 2>&1; then
        echo "FAIL   z80 rejects invalid arithmetic/logical form: $bad"
        fail=$((fail + 1))
    else
        pass=$((pass + 1))
    fi
done

echo "$pass ok, $fail failed"
[ "$fail" -eq 0 ]
