# Compiler and flags
CC = gcc   # or cl.exe for MSVC

# Target platform selection now happens at runtime (mini-asm -t <target>),
# not at build time, so a single binary bundles every output writer.
# See src/platform.c for the list of available -t targets.
CFLAGS = -Wall -Wextra -I./src

# Source and output
SRC = $(wildcard src/*.c)
ifeq ($(OS),Windows_NT)
    OUT = mini-asm.exe
else
    OUT = mini-asm
endif

# Default target
all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OUT)
	
