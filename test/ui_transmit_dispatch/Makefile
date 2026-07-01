# From repo root: make -C test/ui_transmit_dispatch

CC ?= cc
TEST_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
OBJ_DIR = build
TARGET = ui_transmit_dispatch_test

REPO_ROOT := $(abspath $(TEST_DIR)/../..)
SRC_DIR   := $(REPO_ROOT)/src
SRC2_DIR  := $(REPO_ROOT)/src2

CFLAGS = -std=c11 -Wall -Wextra -g \
	-I$(SRC2_DIR) -I$(SRC_DIR) -I$(REPO_ROOT)
LDFLAGS = -Wl,-dead_strip -lm

SRCS = $(TEST_DIR)/main.c \
	$(SRC2_DIR)/ui/uitree.c \
	$(SRC2_DIR)/ui/ui_behavior.c \
	$(SRC2_DIR)/vm/cs1vm.c \
	$(SRC2_DIR)/vm/cs1vm_host.c \
	$(SRC2_DIR)/vm/cs1vm_level.c \
	$(SRC2_DIR)/vm/cs2_script.c \
	$(SRC2_DIR)/vm/cs2vm.c \
	$(SRC2_DIR)/vm/cs2_opcode_meta.c \
	$(SRC2_DIR)/vm/cs2_host_ui.c \
	$(SRC2_DIR)/toriauxlib/vm/toriauxlibvm.c \
	$(SRC2_DIR)/toriauxlib/core/toriauxlibcore.c \
	$(SRC2_DIR)/toriauxlib/core/toriauxlibcore_types.c \
	$(SRC2_DIR)/toridraw/toridraw_map.c \
	$(SRC2_DIR)/toridraw/toridraw_vec.c \
	$(SRC_DIR)/osrs/varp_varbit_manager.c

OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.c=.o)))

.PHONY: all clean run check

all: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

define make-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1) | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $(1) -o $$@
endef

$(foreach src,$(SRCS),$(eval $(call make-goal,$(src))))

run check: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
