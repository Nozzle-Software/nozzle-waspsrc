# =============================================================================
#  Quake SDK Toolchain
#  Tools: vbsp, waspakr, mdl, waspcompiler
# =============================================================================

CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Wpedantic -O2
LDFLAGS = -lX11 -lm

# WaspCompiler needs c11 for its own sources
WASP_CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -O2 \
              -Wno-unused-parameter -Wno-unused-function

# -----------------------------------------------------------------------------
# Directories
# -----------------------------------------------------------------------------
BINDIR  = bin
OBJDIR  = obj
WASPOBJ = $(OBJDIR)/wasp

# -----------------------------------------------------------------------------
# Targets
# -----------------------------------------------------------------------------
TARGETS = $(BINDIR)/vbsp        \
          $(BINDIR)/mdl         \
          $(BINDIR)/waspcompiler \
          $(BINDIR)/waspakr

# -----------------------------------------------------------------------------
# Sources
# -----------------------------------------------------------------------------
VBSP_SRC = vbsp/vbsp.c
MDL_SRC    = mdl/mdl.c
WASPAKR_SRC = waspakr/waspakr.c

WASP_SRCS = waspcompiler/src/main.c    \
            waspcompiler/src/common.c  \
            waspcompiler/src/lexer.c   \
            waspcompiler/src/ast.c     \
            waspcompiler/src/symtab.c  \
            waspcompiler/src/parser.c  \
            waspcompiler/src/codegen.c

WASP_OBJS = $(patsubst waspcompiler/src/%.c, $(WASPOBJ)/%.o, $(WASP_SRCS))

# -----------------------------------------------------------------------------
# Default target
# -----------------------------------------------------------------------------
.PHONY: all clean wasp sdk help

all: $(BINDIR) $(WASPOBJ) $(TARGETS)
	@echo ""
	@echo "  ╔══════════════════════════════════════╗"
	@echo "  ║     Quake SDK Toolchain — Built      ║"
	@echo "  ╠══════════════════════════════════════╣"
	@echo "  ║  bin/vbsp          BSP compiler      ║"
	@echo "  ║  bin/waspakr       PAK archiver      ║"
	@echo "  ║  bin/mdl           MDL converter     ║"
	@echo "  ║  bin/waspcompiler  QuakeC compiler   ║"
	@echo "  ╚══════════════════════════════════════╝"
	@echo ""

# Convenience aliases
sdk:  $(BINDIR) $(BINDIR)/vbsp $(BINDIR)/pak $(BINDIR)/mdl $(BINDIR)/waspakr
wasp: $(BINDIR) $(WASPOBJ) $(BINDIR)/waspcompiler

# -----------------------------------------------------------------------------
# Directory creation
# -----------------------------------------------------------------------------
$(BINDIR):
	mkdir -p $(BINDIR)

$(WASPOBJ):
	mkdir -p $(WASPOBJ)

# -----------------------------------------------------------------------------
# SDK tools
# -----------------------------------------------------------------------------
$(BINDIR)/vbsp: $(VBSP_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "  [OK] vbsp"

$(BINDIR)/mdl: $(MDL_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^ -lm
	@echo "  [OK] mdl"

# -----------------------------------------------------------------------------
# WasPakr — Quake PAK archiver
# -----------------------------------------------------------------------------
$(BINDIR)/waspakr: $(WASPAKR_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "  [OK] waspakr"

# -----------------------------------------------------------------------------
# WaspCompiler — compiled with c11, separate obj directory
# -----------------------------------------------------------------------------
$(WASPOBJ)/%.o: waspcompiler/src/%.c | $(WASPOBJ)
	$(CC) $(WASP_CFLAGS) -Iwaspcompiler/src -c $< -o $@

$(BINDIR)/waspcompiler: $(WASP_OBJS) | $(BINDIR)
	$(CC) $(WASP_CFLAGS) -o $@ $^ -lm
	@echo "  [OK] waspcompiler"

# -----------------------------------------------------------------------------
# WasPakr — Quake PAK archiver
# -----------------------------------------------------------------------------
$(BINDIR)/waspakr: $(WASPAKR_SRC) | $(BINDIR)
	$(CC) $(CFLAGS) -o $@ $^
	@echo "  [OK] waspakr"

# -----------------------------------------------------------------------------
# WaspCompiler object dependencies
# -----------------------------------------------------------------------------
$(WASPOBJ)/main.o:    waspcompiler/src/main.c    \
                      waspcompiler/src/common.h  \
                      waspcompiler/src/lexer.h   \
                      waspcompiler/src/parser.h  \
                      waspcompiler/src/codegen.h

$(WASPOBJ)/common.o:  waspcompiler/src/common.c  waspcompiler/src/common.h
$(WASPOBJ)/lexer.o:   waspcompiler/src/lexer.c   waspcompiler/src/lexer.h   \
                      waspcompiler/src/common.h
$(WASPOBJ)/ast.o:     waspcompiler/src/ast.c     waspcompiler/src/ast.h     \
                      waspcompiler/src/common.h
$(WASPOBJ)/symtab.o:  waspcompiler/src/symtab.c  waspcompiler/src/symtab.h
$(WASPOBJ)/parser.o:  waspcompiler/src/parser.c  waspcompiler/src/parser.h  \
                      waspcompiler/src/lexer.h    waspcompiler/src/ast.h
$(WASPOBJ)/codegen.o: waspcompiler/src/codegen.c waspcompiler/src/codegen.h \
                      waspcompiler/src/ast.h      waspcompiler/src/symtab.h  \
                      waspcompiler/src/progs.h    waspcompiler/src/lexer.h

# -----------------------------------------------------------------------------
# Clean
# -----------------------------------------------------------------------------
clean:
	rm -rf $(BINDIR) $(OBJDIR)
	@echo "  Cleaned."

# -----------------------------------------------------------------------------
# Help
# -----------------------------------------------------------------------------
help:
	@echo ""
	@echo "  WaspSrc SDK Toolchain — Targets"
	@echo ""
	@echo "  make             Build everything"
	@echo "  make sdk         Build only vbsp, waspakr, mdl"
	@echo "  make wasp        Build only waspcompiler"
	@echo "  make clean       Remove bin/ and obj/"
	@echo "  make help        Show this message"
	@echo ""
	@echo "  Tools:"
	@echo "    bin/vbsp           WaspSrc map editor"
	@echo "    bin/wasppakr       PAK file archiver"
	@echo "    bin/mdl            MDL model converter"
	@echo "    bin/waspcompiler   QuakeC compiler (progs.dat output)"
	@echo ""
