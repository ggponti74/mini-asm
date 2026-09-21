# Compiler and flags
CC = i686-w64-mingw32-gcc
CFLAGS = -Wall -Wextra -I./src -m32

# Source and output
SRC = $(wildcard src/*.c)
OUT = mini-asm.exe

# Default target
all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OUT)
