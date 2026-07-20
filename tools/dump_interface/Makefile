# Standalone build (no sanitizers). Prefer this or CMake dump targets; both avoid
# the macOS ASAN hang that hits when CMAKE_C_FLAGS includes -fsanitize=address.
CC = clang
OBJ_DIR = build
TARGET = dump_interface

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
V0_DIR    := $(REPO_ROOT)/v0
SRC_DIR   := $(REPO_ROOT)/src
TOOLS_DIR := $(REPO_ROOT)/tools

CFLAGS = -std=c11 -Wall -Wextra -g \
	-I$(TOOLS_DIR) -I$(V0_DIR) -I$(V0_DIR)/osrs/rscache -I$(SRC_DIR)

LDFLAGS = -lbz2 -lz

ifdef ASAN
CFLAGS += -fno-omit-frame-pointer
LDFLAGS +=
endif

SRCS = main.c \
	$(TOOLS_DIR)/dump_interface_common.c \
	$(V0_DIR)/osrs/rscache/unity.c \
	$(V0_DIR)/3rd/bzip/bzip.c \
	$(V0_DIR)/3rd/miniz/miniz.c

OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.c=.o)))

# dump_interface_common.o must not collide with dump_interface_layout build
COMMON_OBJ = $(OBJ_DIR)/dump_interface_common.o

all: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(OBJ_DIR)/main.o: main.c
	$(CC) $(CFLAGS) -c main.c -o $@

$(COMMON_OBJ): $(TOOLS_DIR)/dump_interface_common.c
	$(CC) $(CFLAGS) -c $(TOOLS_DIR)/dump_interface_common.c -o $@

$(OBJ_DIR)/unity.o: $(V0_DIR)/osrs/rscache/unity.c
	$(CC) $(CFLAGS) -c $(V0_DIR)/osrs/rscache/unity.c -o $@

$(OBJ_DIR)/bzip.o: $(V0_DIR)/3rd/bzip/bzip.c
	$(CC) $(CFLAGS) -c $(V0_DIR)/3rd/bzip/bzip.c -o $@

$(OBJ_DIR)/miniz.o: $(V0_DIR)/3rd/miniz/miniz.c
	$(CC) $(CFLAGS) -c $(V0_DIR)/3rd/miniz/miniz.c -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean
