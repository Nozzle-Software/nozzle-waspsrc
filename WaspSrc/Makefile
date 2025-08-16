# WaspSrc Makefile for Linux
# Its recomended that you don't screw up the build :)

program_NAME := WaspSrc
program_C_SRCS := $(wildcard *.c)
program_OBJS := ${program_C_SRCS:.c=.o}
program_LIBRARIES := X11 Xext m

CFLAGS := -Wall -m32 -O2 -fcommon

LDFLAGS += -m32
LDFLAGS += $(foreach library,$(program_LIBRARIES),-l$(library))

.PHONY: all clean distclean

all: $(program_NAME)
wasp: $(program_NAME)

$(program_NAME): $(program_OBJS)
	$(CC) $(program_OBJS) -o $(program_NAME) $(LDFLAGS)

clean:
	@- $(RM) $(program_NAME)
	@- $(RM) $(program_OBJS)

distclean: clean
