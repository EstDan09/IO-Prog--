# ===== Makefile - Proyecto Simplex (CLI + GTK/Glade GUI) =====

# === Compilador y herramientas ===
CC       := gcc
AR       := ar
CSTD     := -std=c11
WARN     := -Wall -Wextra -Wpedantic -Wshadow -Wpointer-arith -Wconversion
OPT      := -O2
DBG      := -g3
DEP      := -MMD -MP

# === Directorios ===
BUILDDIR := build
BINDIR   := bin

# === Flags generales ===
CFLAGS   := $(CSTD) $(WARN) $(OPT) $(DEP)
CPPFLAGS := -I.
LIBS     := -lm
LDFLAGS  :=

# === GTK (para la interfaz Glade) ===
GTK_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GTK_LIBS   := $(shell pkg-config --libs gtk+-3.0)

# === Archivos fuente ===
LIBSRC := simplex_report.c
LIBOBJ := $(addprefix $(BUILDDIR)/,$(LIBSRC:.c=.o))
LIBDEP := $(LIBOBJ:.o=.d)
LIBA   := $(BUILDDIR)/libsimplex.a

APPSRC := main.c
APPOBJ := $(addprefix $(BUILDDIR)/,$(APPSRC:.c=.o))
APPDEP := $(APPOBJ:.o=.d)
APP    := $(BINDIR)/simplex_cli

GUISRC := simplex_gui.c
GUIOBJ := $(addprefix $(BUILDDIR)/,$(GUISRC:.c=.o))
GUIDEP := $(GUIOBJ:.o=.d)
GUIAPP := $(BINDIR)/simplex_gui

# === Configuración multiplataforma ===
ifeq ($(OS),Windows_NT)
  MKDIR_P = if not exist "$@" mkdir "$@"
  RM_RF   := rmdir /S /Q
else
  MKDIR_P = mkdir -p "$@"
  RM_RF   := rm -rf
endif

# === Targets públicos ===
.PHONY: all lib app gui debug asan clean run run-gui pdf

all: lib app gui

lib: $(LIBA)
app: $(APP)
gui: $(GUIAPP)

# === Librería estática (núcleo Simplex) ===
$(LIBA): $(LIBOBJ) | $(BUILDDIR)
	$(AR) rcs $@ $(LIBOBJ)
	@echo ">> Librería creada: $@"

# === Aplicación CLI ===
$(APP): $(APPOBJ) $(LIBA) | $(BINDIR)
	$(CC) $(CFLAGS) $(APPOBJ) -L$(BUILDDIR) -lsimplex $(LIBS) -o $@
	@echo ">> Binario CLI creado: $@"

# === Aplicación GUI (GTK + Glade) ===
$(GUIAPP): $(GUIOBJ) $(LIBA) | $(BINDIR)
	$(CC) $(CFLAGS) $(GTK_CFLAGS) $(GUIOBJ) -L$(BUILDDIR) -lsimplex $(LDFLAGS) $(LIBS) $(GTK_LIBS) -o $@
	@echo ">> Binario GUI creado: $@"

# === Crear directorios ===
$(BUILDDIR) $(BINDIR):
	$(MKDIR_P)

# === Compilación genérica ===
# Agrega automáticamente los include paths de GTK al compilar simplex_gui.c
$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	@if echo \"$<\" | grep -q \"simplex_gui.c\"; then \
		echo \"[GTK] Compilando $<\"; \
		$(CC) $(CFLAGS) $(CPPFLAGS) $(GTK_CFLAGS) -c $< -o $@; \
	else \
		$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@; \
	fi

# === Modos especiales de compilación ===
debug: CFLAGS := $(CSTD) $(WARN) -O0 $(DBG) $(DEP)
debug: clean all

asan: CFLAGS := $(CSTD) $(WARN) -O0 $(DBG) $(DEP) -fsanitize=address,undefined
asan: LDFLAGS += -fsanitize=address,undefined
asan: clean all

# === Ejecución ===
run: $(APP)
	./$(APP)

run-gui: $(GUIAPP)
	./$(GUIAPP)

# === Generación del PDF (usando LaTeX) ===
TEX ?= reporte_simplex.tex
pdf: $(APP)
	@test -f "$(TEX)" || (echo "No existe $(TEX). Ejecuta primero el binario para generarlo."; exit 1)
	pdflatex -interaction=nonstopmode "$(TEX)" >/dev/null || true
	@echo ">> PDF compilado: $(TEX:.tex=.pdf)"

# === Limpieza ===
clean:
	-$(RM_RF) "$(BUILDDIR)" "$(BINDIR)"
	-rm -f *.aux *.log *.out *.toc *.pdf *.d

# === Dependencias automáticas ===
-include $(LIBDEP) $(APPDEP) $(GUIDEP)
