# CPU-only modelarena/pose/atlas leak test.
# From repo root: make -C test/trspk_leak

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
PK_CORE := $(ROOT)/src2/platformkit/core

CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -g
INCLUDES := -I$(ROOT)/src -I$(ROOT)/src2

LDFLAGS := -lm

SOURCES := trspk_leak_test.c \
	$(PK_CORE)/trspk_atlas.c \
	$(PK_CORE)/trspk_modelarena.c \
	$(PK_CORE)/trspk_pose.c \
	$(PK_CORE)/trspk_triangles.c \
	$(PK_CORE)/trspk_vbo.c \
	$(PK_CORE)/trspk_vbochain16.c

TARGET := trspk_leak_test

.PHONY: all clean check test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(SOURCES) $(LDFLAGS)

clean:
	rm -f $(TARGET)

check test: $(TARGET)
	./$(TARGET)
