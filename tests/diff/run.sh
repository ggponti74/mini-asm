#!/bin/sh
# Differential test of the 68K logic, arithmetic and shift instructions against a real 68K:
# the same source is assembled by mini-asm (elf, arm, and PE under wine when it works) and by
# GNU as for m68k, run under qemu-m68k, and the recorded results and N/Z/V/C flags must be
# identical. Needs: m68k-linux-gnu-as / -ld (apt: binutils-m68k-linux-gnu), qemu-m68k and
# qemu-arm (apt: qemu-user), python3. Run it from the repo root after "make":
#
#   sh tests/diff/run.sh            all groups (a few minutes with wine)
#   TARGETS=elf,arm sh tests/diff/run.sh unary binary
#                                   groups: unary binary shift mem imm misc
#
# The X flag cannot be read without ROXL/ROXR and is not compared. Memory shifts are
# checked against the manual's rules (memshift_model.py), because qemu-m68k gets those wrong.
cd "$(dirname "$0")/../.." || exit 1
for tool in m68k-linux-gnu-as m68k-linux-gnu-ld qemu-m68k python3; do
    command -v "$tool" >/dev/null 2>&1 || { echo "SKIP   tests/diff   ($tool not installed)"; exit 0; }
done
if [ -z "$TARGETS" ]; then
    TARGETS=elf
    command -v qemu-arm >/dev/null 2>&1 && TARGETS=$TARGETS,arm
    if command -v wine >/dev/null 2>&1; then TARGETS=$TARGETS,pe; fi
fi
export TARGETS
status=0
python3 tests/diff/diff.py "$@" || status=1
python3 tests/diff/memshift_model.py || status=1
exit $status
