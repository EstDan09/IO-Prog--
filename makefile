CC=gcc
CFLAGS=$(shell pkg-config --cflags gtk+-3.0) -Wall -Wextra -g
LDFLAGS=$(shell pkg-config --libs gtk+-3.0)

BIN_DIR=bin
SRC_DIR=src

all: $(BIN_DIR)/pending $(BIN_DIR)/menu

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/pending: $(SRC_DIR)/pending.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BIN_DIR)/menu: $(SRC_DIR)/menu.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

run-pending: all
	./bin/pending

run-menu: all
	./bin/menu

clean:
	rm -rf $(BIN_DIR)
