# Standalone build (no CMake ASAN). See tools/dump_interface/Makefile.
CC = clang
OBJ_DIR = build
TARGET = dump_graphic

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SRC_DIR   := $(REPO_ROOT)/src
SRC2_DIR  := $(REPO_ROOT)/src2
TOOLS_DIR := $(REPO_ROOT)/tools

CFLAGS = -std=c11 -Wall -Wextra -g \
	-I$(TOOLS_DIR) -I$(SRC2_DIR) -I$(SRC_DIR)

LDFLAGS = -lbz2 -lz

ifdef ASAN
CFLAGS += -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
endif

SRCS = main.c \
	$(SRC_DIR)/osrs/rscache/unity.c \
	$(SRC_DIR)/bmp.c \
	$(SRC_DIR)/3rd/bzip/bzip.c \
	$(SRC_DIR)/3rd/miniz/miniz.c

OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.c=.o)))

all: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(OBJ_DIR)/main.o: main.c
	$(CC) $(CFLAGS) -c main.c -o $@

$(OBJ_DIR)/unity.o: $(SRC_DIR)/osrs/rscache/unity.c
	$(CC) $(CFLAGS) -c $(SRC_DIR)/osrs/rscache/unity.c -o $@

$(OBJ_DIR)/bmp.o: $(SRC_DIR)/bmp.c
	$(CC) $(CFLAGS) -c $(SRC_DIR)/bmp.c -o $@

$(OBJ_DIR)/bzip.o: $(SRC_DIR)/3rd/bzip/bzip.c
	$(CC) $(CFLAGS) -c $(SRC_DIR)/3rd/bzip/bzip.c -o $@

$(OBJ_DIR)/miniz.o: $(SRC_DIR)/3rd/miniz/miniz.c
	$(CC) $(CFLAGS) -c $(SRC_DIR)/3rd/miniz/miniz.c -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean
