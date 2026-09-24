# Compiler and flags
CC = gcc   # or cl.exe for MSVC

# Target architecture: ARM (default) or X86
# Build with: make TARGET=X86
TARGET ?= ARM
CFLAGS = -Wall -Wextra -I./src -DTARGET_$(TARGET)

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
	
