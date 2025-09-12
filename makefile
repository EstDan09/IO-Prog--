CC=gcc
CFLAGS=$(shell pkg-config --cflags gtk+-3.0) -Wall -Wextra -g
LDFLAGS=$(shell pkg-config --libs gtk+-3.0)

BIN_DIR=bin
SRC_DIR=src
P1_SRC_DIR=p1/src
P1_UI_DIR=p1/ui

all: $(BIN_DIR)/pending $(BIN_DIR)/menu $(BIN_DIR)/p1 $(BIN_DIR)/floyd

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/pending: $(SRC_DIR)/pending.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BIN_DIR)/menu: $(SRC_DIR)/menu.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# --- NEW RULE FOR P1 ---
$(BIN_DIR)/p1: $(P1_SRC_DIR)/file.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# --- NEW RULE FOR FLOYD ---
$(BIN_DIR)/floyd: $(P1_SRC_DIR)/floyd.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# --- RUN COMMANDS ---
run-pending: $(BIN_DIR)/pending
	./bin/pending

run-menu: $(BIN_DIR)/menu $(BIN_DIR)/floyd
	./bin/menu

run-p1: $(BIN_DIR)/p1
	./bin/p1 $(P1_UI_DIR)/file.glade

run-floyd: $(BIN_DIR)/floyd
	./bin/floyd $(P1_UI_DIR)/floyd.glade

clean:
	rm -rf $(BIN_DIR)
