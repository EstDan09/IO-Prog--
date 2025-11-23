# =====================================================
#  Simplex GUI - Makefile
# =====================================================

# Compiler and flags
CC       := gcc
CFLAGS   := $(shell pkg-config --cflags gtk+-3.0 json-c)
LIBS     := $(shell pkg-config --libs gtk+-3.0 json-c) -lm

# Files and output
SRC      := simplex_gui.c simplex_report.c
TARGET   := simplex_gui
BIN_DIR  := bin

# =====================================================
#  Targets
# =====================================================

# Default build
all: $(BIN_DIR)/$(TARGET)

$(BIN_DIR)/$(TARGET): $(SRC)
	@mkdir -p $(BIN_DIR)
	@echo "Compilando $(TARGET)..."
	$(CC) $(SRC) -o $(BIN_DIR)/$(TARGET) $(CFLAGS) $(LIBS)
	@echo "build completado. Executable: $(BIN_DIR)/$(TARGET)"

# Run the program
run: all
	@./$(BIN_DIR)/$(TARGET)

# Debug build (adds -g for gdb)
debug: CFLAGS += -g
debug: clean all
	@echo "debug listo"

# Clean compiled files
clean:
	@echo "clean build files"
	@rm -rf $(BIN_DIR)

# Phony targets (not actual files)
.PHONY: all run debug clean
