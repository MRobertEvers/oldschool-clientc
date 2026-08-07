# Standalone contract test for the current-v3 reusable 16-bit batch core.
# From the repository root: make -C test/trspk_batch16 check

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)

CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Werror -O2 -g
INCLUDES := -I$(ROOT)/3rd/trspk/core

SOURCES := trspk_batch16_test.c \
	$(ROOT)/3rd/trspk/core/trspk_batch16.c \
	$(ROOT)/3rd/trspk/core/trspk_vbo.c \
	$(ROOT)/3rd/trspk/core/trspk_triangles.c

BUILD_DIR := .build
TARGET := $(BUILD_DIR)/trspk_batch16_test

.PHONY: all clean check test

all: $(TARGET)

$(BUILD_DIR):
	mkdir -p $@

$(TARGET): $(SOURCES) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(SOURCES)

clean:
	rm -rf $(BUILD_DIR)

check test: $(TARGET)
	./$(TARGET)
