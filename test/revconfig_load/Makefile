# From repo root: make -C test/revconfig_load

CC ?= cc
TEST_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
OBJ_DIR = build
TARGET = revconfig_load_test

REPO_ROOT := $(abspath $(TEST_DIR)/../..)
SRC_DIR   := $(REPO_ROOT)/src
SRC2_DIR  := $(REPO_ROOT)/src2

CFLAGS = -std=c11 -Wall -Wextra -g \
	-I$(TEST_DIR) -I$(SRC_DIR) -I$(SRC2_DIR) -I$(SRC2_DIR)/core \
	-I$(SRC2_DIR)/platforms -I$(REPO_ROOT)

LDFLAGS = -Wl,-dead_strip

SRCS = $(TEST_DIR)/main.c \
	$(TEST_DIR)/stubs.c \
	$(SRC2_DIR)/ioqueue/libtorirs_ioqueue.c \
	$(SRC2_DIR)/platforms/platform_x_io_reactor.c \
	$(SRC2_DIR)/toriauxlib/core/tasks/core_task.c \
	$(SRC2_DIR)/revconfig/revconfig.c \
	$(SRC2_DIR)/revconfig/revconfig_load.c \
	$(SRC2_DIR)/3rd/ini/ini.c \
	$(SRC_DIR)/osrs/rscache/shared/shared_string_utils.c

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

$(OBJS): | $(OBJ_DIR)

run: $(TARGET)
	./$(TARGET)

check: run

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
