CC = cc
OBJ_DIR = build
TARGET = async_cache

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SRC_DIR   := $(REPO_ROOT)/src
SRC2_DIR  := $(REPO_ROOT)/src2
PLAT_DIR  := $(SRC2_DIR)/platforms
TOOL_DIR  := $(REPO_ROOT)/tools/async_cache

CFLAGS = -std=c11 -Wall -Wextra -g \
	-I$(SRC_DIR) -I$(SRC2_DIR) -I$(REPO_ROOT) -I$(TOOL_DIR)

LDFLAGS = -lm

SRCS = $(TOOL_DIR)/main.c \
	$(TOOL_DIR)/cachedat2.c \
	$(TOOL_DIR)/async/dat2_model_load.c \
	$(TOOL_DIR)/async/dat2_object_model_load.c \
	$(TOOL_DIR)/async/dat2_reference_table_ensure.c \
	$(PLAT_DIR)/platform_x_io_reactor.c \
	$(PLAT_DIR)/platform_x/cachelib.c \
	$(PLAT_DIR)/platform_x/cachelib_platform.c \
	$(PLAT_DIR)/platform_x/cachelib_serialized.c

THIRD_PARTY_SRCS = \
	$(SRC_DIR)/3rd/bzip/bzip.c \
	$(SRC_DIR)/3rd/miniz/miniz.c

ALL_SRCS = $(SRCS) $(THIRD_PARTY_SRCS)
OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(ALL_SRCS:.c=.o)))

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

THIRD_PARTY_CFLAGS := $(filter-out -Wall -Wextra,$(CFLAGS)) -w

$(OBJ_DIR)/bzip.o: $(SRC_DIR)/3rd/bzip/bzip.c
	$(CC) $(THIRD_PARTY_CFLAGS) -c $< -o $@

$(OBJ_DIR)/miniz.o: $(SRC_DIR)/3rd/miniz/miniz.c
	$(CC) $(THIRD_PARTY_CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
