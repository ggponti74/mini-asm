# Compiler and flags
ifeq ($(OS),Windows_NT)
    CC = gcc   # or cl.exe for MSVC
    # Windows build path, no elf.h
else
    CC = i686-elf-gcc
    # Linux/ELF path
endif

# Target architecture: ARM (default) or X86
# Build with: make TARGET=X86
TARGET ?= ARM
CFLAGS = -Wall -Wextra -I./src -DTARGET_$(TARGET)

# Source and output
SRC = $(wildcard src/*.c)
OUT = mini-asm.exe

# Default target
all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OUT)
	
