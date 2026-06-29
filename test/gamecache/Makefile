# From repo root: make -C test/gamecache

CC ?= cc
OBJ_DIR = build
TARGET = a.out

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SRC_DIR   := $(REPO_ROOT)/src
SRC2_DIR  := $(REPO_ROOT)/src2
TORIAUXLIB_DIR := $(SRC2_DIR)/toriauxlib
TORIDRAW_DIR := $(SRC2_DIR)/toridraw

CFLAGS = -std=c11 -Wall -Wextra -g \
	-I$(SRC_DIR) -I$(SRC2_DIR) -I$(REPO_ROOT)

LDFLAGS = -lm -Wl,-dead_strip

ifeq ($(ENABLE_ASAN),1)
CFLAGS += -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
endif

SRCS = main.c \
	$(TORIAUXLIB_DIR)/core/toriauxlibcore.c \
	$(TORIAUXLIB_DIR)/core/toriauxlibcore_types.c \
	$(TORIDRAW_DIR)/toridraw_map.c \
	$(TORIDRAW_DIR)/toridraw_model.c \
	$(TORIDRAW_DIR)/toridraw_animation.c \
	$(TORIDRAW_DIR)/toridraw_sprite.c

OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.c=.o)))

.PHONY: all clean run

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

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
