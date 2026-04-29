# Standalone trspk_batch16 test (see trspk_batch16_test.c).
# From repo root: make -C test/trspk_batch16

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
TRSPK_TOOLS := $(ROOT)/src/platforms/ToriRSPlatformKit/src/tools

CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -g
INCLUDES := -I$(ROOT)/src \
	-I$(ROOT)/src/platforms/ToriRSPlatformKit/include \
	-I$(TRSPK_TOOLS)

LDFLAGS := -lm

SOURCES := trspk_batch16_test.c \
	$(TRSPK_TOOLS)/trspk_batch16.c \
	$(TRSPK_TOOLS)/trspk_vertex_format.c \
	$(TRSPK_TOOLS)/trspk_vertex_buffer.c \
	$(TRSPK_TOOLS)/trspk_vertex_buffer_lifecycle.c \
	$(TRSPK_TOOLS)/trspk_resource_cache.c \
	$(TRSPK_TOOLS)/trspk_lru_model_cache.c

TARGET := trspk_batch16_test

.PHONY: all clean check test

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) $(INCLUDES) -o $@ $(SOURCES) $(LDFLAGS)

clean:
	rm -f $(TARGET)

check test: $(TARGET)
	./$(TARGET)
