# Tools
CC      = gcc
CFLAGS  = -std=c99 -Wall -Wextra -Wpedantic -O2
LDFLAGS = -lX11 -lm

# Directories
BINDIR = bin

# Targets
TARGETS = $(BINDIR)/vbsp $(BINDIR)/pak $(BINDIR)/mdl

# Sources
VBSP_SRC = vbsp/vbsp.c
PAK_SRC  = pak/pak.c
MDL_SRC  = mdl/mdl.c

.PHONY: all clean

all: $(BINDIR) $(TARGETS)

# Ensure bin directory exists
$(BINDIR):
	mkdir -p $(BINDIR)

# Build vbsp
$(BINDIR)/vbsp: $(VBSP_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Build pak
$(BINDIR)/pak: $(PAK_SRC)
	$(CC) $(CFLAGS) -o $@ $^ -lm

# Build mdl
$(BINDIR)/mdl: $(MDL_SRC)
	$(CC) $(CFLAGS) -o $@ $^ -lm

clean:
	rm -rf $(BINDIR)
