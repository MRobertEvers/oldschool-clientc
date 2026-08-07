# Standalone trspk_atlas dirty-region tests.
# From repo root: make -C test/trspk_atlas check

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)

CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2 -g
INCLUDES := -I$(ROOT)/3rd/trspk/core

SOURCES := trspk_atlas_test.c \
	$(ROOT)/3rd/trspk/core/trspk_atlas.c

TARGET := trspk_atlas_test

.PHONY: all clean check test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(SOURCES)

clean:
	rm -f $(TARGET) $(TARGET).exe

check test: $(TARGET)
	./$(TARGET)
