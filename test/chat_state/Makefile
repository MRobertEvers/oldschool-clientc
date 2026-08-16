# From repo root: make -C test/chat_state

CC ?= cc
OBJ_DIR = build
TARGET = chat_state_test

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SRC2_DIR  := $(REPO_ROOT)/src2

CFLAGS = -std=c11 -Wall -Wextra -g -I$(SRC2_DIR) -I$(REPO_ROOT)
LDFLAGS = -Wl,-dead_strip

SRCS = main.c $(SRC2_DIR)/ui/chat_state.c
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

run check: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
