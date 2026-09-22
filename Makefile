# Compiler and flags
CC = gcc

# Target architecture: ARM (default) or X86
# Build with: make TARGET=X86
TARGET ?= ARM
CFLAGS = -march=native -O3 -Wall -Wextra -I./src -DTARGET_$(TARGET)

# Source and output
SRC = $(wildcard src/*.c)
OUT = mini-asm.exe

# Default target
all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OUT)
	