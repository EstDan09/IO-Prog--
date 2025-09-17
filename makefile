CC=gcc
CFLAGS=$(shell pkg-config --cflags gtk+-3.0) -Wall -Wextra -g
LDFLAGS=$(shell pkg-config --libs gtk+-3.0)

BIN_DIR=bin
SRC_DIR=src
P1_SRC_DIR=p1/src
P1_UI_DIR=p1/ui
P2_SRC_DIR=p2/src
P2_UI_DIR=p2/ui

.PHONY: all clean run-pending run-menu run-p1 run-floyd run-p2

all: $(BIN_DIR)/pending $(BIN_DIR)/menu $(BIN_DIR)/p1 $(BIN_DIR)/floyd $(BIN_DIR)/p2

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(BIN_DIR)/pending: $(SRC_DIR)/pending.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

$(BIN_DIR)/menu: $(SRC_DIR)/menu.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# --- P1 ---
$(BIN_DIR)/p1: $(P1_SRC_DIR)/file.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# --- Floyd (p1) ---
$(BIN_DIR)/floyd: $(P1_SRC_DIR)/floyd.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# --- P2 (knapsack) ---
$(BIN_DIR)/p2: $(P2_SRC_DIR)/knapsack.c | $(BIN_DIR)
	$(CC) $(CFLAGS) $< -o $@ $(LDFLAGS)

# --- RUN COMMANDS ---
run-pending: $(BIN_DIR)/pending
	./bin/pending

# Suele convenir que dependa de los binarios que va a lanzar
run-menu: $(BIN_DIR)/menu $(BIN_DIR)/floyd $(BIN_DIR)/p2
	./bin/menu

run-p1: $(BIN_DIR)/p1
	./bin/p1 $(P1_UI_DIR)/file.glade

run-floyd: $(BIN_DIR)/floyd
	./bin/floyd $(P1_UI_DIR)/floyd.glade

run-p2: $(BIN_DIR)/p2
	./bin/p2 $(P2_UI_DIR)/knapsack.glade

clean:
	rm -rf $(BIN_DIR)

