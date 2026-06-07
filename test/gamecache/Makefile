# From repo root: make -C test/gamecache

CC ?= cc
OBJ_DIR = build
TARGET = a.out

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SRC_DIR   := $(REPO_ROOT)/src
SRC2_DIR  := $(REPO_ROOT)/src2
GAMECACHE_DIR := $(SRC2_DIR)/gamecache
TORIDRAW_DIR := $(SRC2_DIR)/toridraw
PLAT_DIR := $(SRC2_DIR)/platforms

CFLAGS = -std=c11 -Wall -Wextra -g \
	-I$(SRC_DIR) -I$(SRC2_DIR) -I$(REPO_ROOT)

LDFLAGS = -lm -Wl,-dead_strip

SRCS = main.c \
	$(GAMECACHE_DIR)/gamecache.c \
	$(GAMECACHE_DIR)/toridraw_cachemodel.c \
	$(GAMECACHE_DIR)/toridraw_cachesprite.c \
	$(TORIDRAW_DIR)/toridraw_map.c \
	$(TORIDRAW_DIR)/toridraw_model.c \
	$(TORIDRAW_DIR)/toridraw_animation.c \
	$(TORIDRAW_DIR)/toridraw_sprite.c \
	$(SRC_DIR)/osrs/rscache/tables/model.c \
	$(SRC2_DIR)/ioqueue/libtorirs_ioqueue.c \
	$(PLAT_DIR)/platform_x_io_reactor.c \
	$(PLAT_DIR)/platform_x/cachelib.c \
	$(PLAT_DIR)/platform_x/cachelib_platform.c \
	$(PLAT_DIR)/platform_x/cachelib_serialized.c \
	$(SRC_DIR)/osrs/rscache/cache.c \
	$(SRC_DIR)/osrs/rscache/cache_dat.c \
	$(SRC_DIR)/osrs/rscache/reference_table.c \
	$(SRC_DIR)/osrs/rscache/filelist.c \
	$(SRC_DIR)/osrs/rscache/disk.c \
	$(SRC_DIR)/osrs/rscache/rsbuf.c \
	$(SRC_DIR)/osrs/rscache/archive.c \
	$(SRC_DIR)/osrs/rscache/archive_decompress.c \
	$(SRC_DIR)/osrs/rscache/compression.c \
	$(SRC_DIR)/osrs/rscache/xtea.c \
	$(SRC_DIR)/osrs/rscache/xtea_config.c \
	$(SRC_DIR)/osrs/rscache/tables/string_utils.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/config_versionlist_mapsquare.c

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
