# Pixel-exact ARGB blitter differential tests.
# From repo root: make -C test/toridraw_2d check

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)

CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2 -g
INCLUDES := -I$(ROOT)/3rd/toridraw

SOURCES := toridraw_2d_blit_test.c \
	$(ROOT)/3rd/toridraw/toridraw_2d.c

TARGET := toridraw_2d_blit_test

.PHONY: all clean check test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(SOURCES) -lm

clean:
	rm -f $(TARGET) $(TARGET).exe

check test: $(TARGET)
	./$(TARGET)
