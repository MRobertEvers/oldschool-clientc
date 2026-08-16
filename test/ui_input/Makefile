# From repo root: make -C test/ui_input

CC ?= cc
OBJ_DIR = build
TARGET = a.out

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SRC_DIR   := $(REPO_ROOT)/src
SRC2_DIR  := $(REPO_ROOT)/src2

CFLAGS = -std=c11 -Wall -Wextra -g \
	-I$(SRC_DIR) -I$(SRC2_DIR) -I$(REPO_ROOT)

LDFLAGS = -Wl,-dead_strip

SRCS = main.c \
	$(SRC2_DIR)/input/libtorirs_input.c \
	$(SRC2_DIR)/ui/uitree.c \
	$(SRC2_DIR)/ui/uitree_layout.c \
	$(SRC2_DIR)/ui/uitree_host.c \
	$(SRC2_DIR)/ui/ui_input.c \
	$(SRC2_DIR)/ui/ui_behavior.c \
	$(SRC2_DIR)/ui/ui_debug.c \
	$(SRC2_DIR)/ui/ui_scroll.c \
	$(SRC2_DIR)/vm/cs1vm.c \
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

run: $(OBJ_DIR) $(TARGET)
	./$(TARGET)

check: $(OBJ_DIR) $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
