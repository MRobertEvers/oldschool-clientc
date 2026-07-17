# From repo root: make -C src

CC ?= cc
OBJ_DIR = build
TARGET = torirs

REPO_ROOT    := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
SRC_DIR      := $(REPO_ROOT)/src
RSCACHE_DIR  := $(REPO_ROOT)/3rd/rscache
TORIDRAW_DIR := $(REPO_ROOT)/3rd/toridraw
BMP_DIR      := $(REPO_ROOT)/3rd/bmp
HMAP_DIR     := $(REPO_ROOT)/3rd/hmap

CFLAGS = -std=c11 -Wall -Wextra -g \
	-I$(SRC_DIR) -I$(SRC_DIR)/cs2vm2 -I$(REPO_ROOT) -I$(RSCACHE_DIR)/include -I$(RSCACHE_DIR)/src \
	-I$(TORIDRAW_DIR) -I$(BMP_DIR) -I$(HMAP_DIR)

RSCACHE_CFLAGS = -std=c11 -g \
	-I$(RSCACHE_DIR)/include \
	-I$(RSCACHE_DIR)/src \
	-I$(RSCACHE_DIR) \
	-I$(REPO_ROOT)/3rd/bzip \
	-I$(REPO_ROOT)/3rd/miniz \
	-I$(REPO_ROOT)/3rd/xteas

TORIDRAW_CFLAGS = -std=c11 -g \
	-I$(TORIDRAW_DIR) \
	-I$(BMP_DIR)

LDFLAGS = -lm -Wl,-dead_strip

ifeq ($(ENABLE_ASAN),1)
CFLAGS += -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
endif

SRCS = main.c platform/platform_x_io.c $(BMP_DIR)/bmp.c \
	cs2vm2/cs2vm2.c cs2vm2/cs2vm2_script.c cs2vm2/cs2vm2_trigger_args.c \
	cs2vm2/cs2_opcode_meta.c engine/cs2vm2_script_from_rscache.c \
	engine/dat1/dat1_buildcache.c engine/dat2/dat2_buildcache.c
RSCACHE_UNITY_SRC = $(RSCACHE_DIR)/rscache_unity.c
TORIDRAW_UNITY_SRC = $(TORIDRAW_DIR)/toridraw_unity.c
OBJS = $(OBJ_DIR)/main.o $(OBJ_DIR)/platform_x_io.o $(OBJ_DIR)/bmp.o \
	$(OBJ_DIR)/cs2vm2.o $(OBJ_DIR)/cs2vm2_script.o $(OBJ_DIR)/cs2vm2_trigger_args.o \
	$(OBJ_DIR)/cs2_opcode_meta.o $(OBJ_DIR)/cs2vm2_script_from_rscache.o \
	$(OBJ_DIR)/dat1_buildcache.o $(OBJ_DIR)/dat2_buildcache.o \
	$(OBJ_DIR)/hmap.o $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/toridraw_unity.o

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

$(OBJ_DIR)/hmap.o: $(HMAP_DIR)/hmap.c
	$(CC) $(CFLAGS) -c $(HMAP_DIR)/hmap.c -o $@

$(OBJ_DIR)/rscache_unity.o: $(RSCACHE_UNITY_SRC)
	$(CC) $(RSCACHE_CFLAGS) -w -c $(RSCACHE_UNITY_SRC) -o $@

$(OBJ_DIR)/toridraw_unity.o: $(TORIDRAW_UNITY_SRC)
	$(CC) $(TORIDRAW_CFLAGS) -w -c $(TORIDRAW_UNITY_SRC) -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
