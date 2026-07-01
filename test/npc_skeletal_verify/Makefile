# From repo root: make -C test/npc_skeletal_verify

CC ?= cc
TEST_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
OBJ_DIR = build
TARGET = npc_skeletal_verify

REPO_ROOT := $(abspath $(TEST_DIR)/../..)
SRC_DIR   := $(REPO_ROOT)/src
SRC2_DIR  := $(REPO_ROOT)/src2
SDL2_OBJ  := $(SRC2_DIR)/programs/sdl2/sdl2_build

CFLAGS = -std=c11 -Wall -Wextra -g -MMD -MP \
	-I$(TEST_DIR) -I$(SRC_DIR) -I$(SRC2_DIR) -I$(SRC2_DIR)/core \
	-I$(SRC2_DIR)/toridraw -I$(SRC2_DIR)/platforms \
	-I$(SRC2_DIR)/toriauxlib/core/tasks -I$(REPO_ROOT)

LDFLAGS = -Wl,-dead_strip -lm

MAIN_OBJ = $(OBJ_DIR)/main.o
SDL2_OBJS = $(filter-out $(SDL2_OBJ)/sdl2.o,$(wildcard $(SDL2_OBJ)/*.o))

.PHONY: all clean run

all: $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(TEST_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(MAIN_OBJ) $(SDL2_OBJS)
	$(CC) $(CFLAGS) -o $@ $(MAIN_OBJ) $(SDL2_OBJS) $(LDFLAGS)

run: $(TARGET)
	./$(TARGET) $(REPO_ROOT)/cache 12204

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

-include $(OBJ_DIR)/*.d
