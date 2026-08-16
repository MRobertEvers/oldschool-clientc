# From repo root: make -C test/cs2_runtime

CC ?= cc
OBJ_DIR = build
TARGET = cs2_runtime_test

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SRC2_DIR  := $(REPO_ROOT)/src2
SRC_DIR   := $(REPO_ROOT)/src

CFLAGS = -std=c11 -Wall -Wextra -g \
	-I$(SRC2_DIR) -I$(SRC_DIR) -I$(REPO_ROOT)
LDFLAGS = -Wl,-dead_strip -lm

SRCS = main.c stubs.c \
	$(SRC2_DIR)/vm/cs2vm_unity.c \
	$(SRC2_DIR)/vm/cs1vm.c \
	$(SRC2_DIR)/games/ie_enum_lookup.c \
	$(SRC2_DIR)/games/ie_struct_lookup.c \
	$(SRC2_DIR)/ui/uitree.c \
	$(SRC2_DIR)/toridraw/toridraw_font.c \
	$(SRC_DIR)/osrs/rscache/shared/shared_rs_buffer.c \
	$(SRC_DIR)/osrs/rscache/shared/shared_file_list.c

.PHONY: all clean run check

all: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LDFLAGS)

run check: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
