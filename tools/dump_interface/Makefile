# Standalone build (no sanitizers). Prefer this or CMake dump targets; both avoid
# the macOS ASAN hang that hits when CMAKE_C_FLAGS includes -fsanitize=address.
CC = clang
OBJ_DIR = build
TARGET = dump_interface

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
RSCACHE   := $(REPO_ROOT)/3rd/rscache
THIRD     := $(REPO_ROOT)/3rd
SRC_DIR   := $(REPO_ROOT)/src
TOOLS_DIR := $(REPO_ROOT)/tools

CFLAGS = -std=c11 -Wall -Wextra -g \
	-I$(TOOLS_DIR) -I$(SRC_DIR) -I$(RSCACHE)/include -I$(RSCACHE)/src -I$(RSCACHE) \
	-I$(THIRD)/bzip -I$(THIRD)/miniz -I$(THIRD)/xteas

LDFLAGS = -lbz2 -lz -lm

ifdef ASAN
CFLAGS += -fno-omit-frame-pointer
LDFLAGS +=
endif

SRCS = main.c \
	$(TOOLS_DIR)/dump_interface_common.c \
	$(RSCACHE)/rscache_unity.c \
	$(THIRD)/bzip/bzip.c \
	$(THIRD)/bzip/bzip_encode.c \
	$(THIRD)/miniz/miniz.c \
	$(THIRD)/xteas/xteas.c

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

$(OBJ_DIR)/rscache_unity.o: $(RSCACHE)/rscache_unity.c
	$(CC) $(CFLAGS) -c $(RSCACHE)/rscache_unity.c -o $@

$(OBJ_DIR)/bzip.o: $(THIRD)/bzip/bzip.c
	$(CC) $(CFLAGS) -c $(THIRD)/bzip/bzip.c -o $@

$(OBJ_DIR)/bzip_encode.o: $(THIRD)/bzip/bzip_encode.c
	$(CC) $(CFLAGS) -c $(THIRD)/bzip/bzip_encode.c -o $@

$(OBJ_DIR)/miniz.o: $(THIRD)/miniz/miniz.c
	$(CC) $(CFLAGS) -c $(THIRD)/miniz/miniz.c -o $@

$(OBJ_DIR)/xteas.o: $(THIRD)/xteas/xteas.c
	$(CC) $(CFLAGS) -c $(THIRD)/xteas/xteas.c -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean
