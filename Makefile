CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -O2
INCLUDES = -Iinclude

SRC := $(wildcard src/*.c)
OBJ := $(patsubst src/%.c,build/%.o,$(SRC))
TARGET := build/j2c2vhdl

# test helpers
LEXER_DUMP := build/lexer_dump
LEXER_DUMP_SRC := tests/lexer_dump.c src/lexer.c src/diag.c

PARSER_DUMP := build/parser_dump
PARSER_DUMP_SRC := tests/parser_dump.c src/lexer.c src/parser.c src/ast.c src/diag.c

.PHONY: all clean test lexerdump parserdump

all: $(TARGET)

build:
	mkdir -p build

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(TARGET): $(OBJ) | build
	$(CC) $(CFLAGS) $(INCLUDES) $(OBJ) -o $(TARGET)

$(LEXER_DUMP): $(LEXER_DUMP_SRC) | build
	$(CC) $(CFLAGS) $(INCLUDES) $(LEXER_DUMP_SRC) -o $(LEXER_DUMP)

lexerdump: $(LEXER_DUMP)

$(PARSER_DUMP): $(PARSER_DUMP_SRC) | build
	$(CC) $(CFLAGS) $(INCLUDES) $(PARSER_DUMP_SRC) -o $(PARSER_DUMP)

parserdump: $(PARSER_DUMP)

test: all lexerdump parserdump
	@echo "Run: ./build/lexer_dump <file.java>"
	@echo "Run: ./build/parser_dump <file.java>"

clean:
	rm -rf build
