CC      ?= gcc
CFLAGS  ?= -Wall -Wextra
SRC      = src/ttymidi.c
BIN      = ttymidi

all:
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) -lasound

clean:
	rm -f $(BIN)

install:
	install -m 0755 $(BIN) /usr/local/bin

uninstall:
	rm /usr/local/bin/$(BIN)

format:
	clang-format -i $(SRC)

lint:
	cppcheck --enable=all --std=c11 --inline-suppr --suppress=missingIncludeSystem $(SRC)

.PHONY: all clean install uninstall format lint
