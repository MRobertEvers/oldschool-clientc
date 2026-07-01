# From repo root: make -C test/ui_tree

CC ?= cc
TEST_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
OBJ_DIR = build
TARGET = ui_tree_test

REPO_ROOT := $(abspath $(TEST_DIR)/../..)
SRC_DIR   := $(REPO_ROOT)/src
SRC2_DIR  := $(REPO_ROOT)/src2

CFLAGS = -std=c11 -Wall -Wextra -g \
	-I$(TEST_DIR) -I$(SRC_DIR) -I$(SRC2_DIR) -I$(REPO_ROOT)

LDFLAGS = -Wl,-dead_strip

SRCS = $(TEST_DIR)/main.c \
	$(TEST_DIR)/stubs.c \
	$(TEST_DIR)/test_sprites.c \
	$(TEST_DIR)/sidenav_fixture.c \
	$(TEST_DIR)/ui_tree_draw.c \
	$(SRC2_DIR)/ui/uitree.c \
	$(SRC2_DIR)/ui/uitree_layout.c \
	$(SRC2_DIR)/ui/uitree_host.c \
	$(SRC2_DIR)/ui/ui_input.c \
	$(SRC2_DIR)/ui/ui_scroll.c \
	$(SRC2_DIR)/ui/ui_chat_minimenu.c \
	$(SRC2_DIR)/ui/ui_behavior.c \
	$(SRC2_DIR)/vm/csvm.c \
	$(SRC2_DIR)/toridraw/toridraw_sprite.c \
	$(SRC2_DIR)/toridraw/toridraw_math.c \
	$(SRC_DIR)/bmp.c \
	$(SRC_DIR)/osrs/varp_varbit_manager.c

OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.c=.o)))

.PHONY: all clean run check

all: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

define make-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1)
	$(CC) $(CFLAGS) -c $(1) -o $$@
endef

$(foreach src,$(SRCS),$(eval $(call make-goal,$(src))))

run: $(TARGET)
	./$(TARGET)

check: run

clean:
	rm -rf $(OBJ_DIR) $(TARGET) output
