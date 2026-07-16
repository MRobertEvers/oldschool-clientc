# From repo root: make -C src

CC ?= cc
OBJ_DIR = build
TARGET = torirs

REPO_ROOT   := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
SRC_DIR     := $(REPO_ROOT)/src
RSCACHE_DIR := $(REPO_ROOT)/3rd/rscache

CFLAGS = -std=c11 -Wall -Wextra -g \
	-I$(SRC_DIR) -I$(REPO_ROOT) -I$(RSCACHE_DIR)/include -I$(RSCACHE_DIR)/src

RSCACHE_CFLAGS = -std=c11 -g \
	-I$(RSCACHE_DIR)/include \
	-I$(RSCACHE_DIR)/src \
	-I$(RSCACHE_DIR) \
	-I$(REPO_ROOT)/3rd/bzip \
	-I$(REPO_ROOT)/3rd/miniz \
	-I$(REPO_ROOT)/3rd/xteas

LDFLAGS = -lm -Wl,-dead_strip

ifeq ($(ENABLE_ASAN),1)
CFLAGS += -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
endif

SRCS = main.c platform/platform_x_io.c
UNITY_SRC = $(RSCACHE_DIR)/rscache_unity.c
OBJS = $(OBJ_DIR)/main.o $(OBJ_DIR)/platform_x_io.o $(OBJ_DIR)/rscache_unity.o

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

$(OBJ_DIR)/rscache_unity.o: $(UNITY_SRC)
	$(CC) $(RSCACHE_CFLAGS) -w -c $(UNITY_SRC) -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
