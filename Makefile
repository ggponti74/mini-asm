# Compiler and flags
CC = gcc
CFLAGS = -Wall -Wextra -I./src

# Source and output
SRC = $(wildcard src/*.c)
OUT = mini-asm.exe

# Default target
all: $(OUT)

$(OUT): $(SRC)
	$(CC) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OUT)
