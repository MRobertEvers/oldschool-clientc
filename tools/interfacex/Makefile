CC = clang
OBJ_DIR = build
TARGET = interfacex

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SRC_DIR   := $(REPO_ROOT)/src
SRC2_DIR  := $(REPO_ROOT)/src2
TOOL_DIR  := $(REPO_ROOT)/tools/interfacex

CFLAGS = -std=c11 -Wall -Wextra -g \
	-I$(TOOL_DIR) -I$(SRC2_DIR)/toridraw -I$(SRC2_DIR)/toriauxlib/td -I$(SRC2_DIR) -I$(SRC_DIR)

LDFLAGS = -lm

ifdef ASAN
CFLAGS += -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
endif

SRCS = $(TOOL_DIR)/main.c \
	$(SRC_DIR)/bmp.c \
	$(SRC2_DIR)/toridraw/toridraw_unity.c \
	$(SRC2_DIR)/toridraw/toridraw_model_sprite.c \
	$(SRC2_DIR)/toridraw/toridraw_font.c \
	$(SRC2_DIR)/toriauxlib/td/toridraw_cachemodel.c \
	$(SRC2_DIR)/toriauxlib/core/toriauxlibcore_types.c \
	$(SRC2_DIR)/toriauxlib/c/toriauxlibcache_clientscript_convert.c \
	$(SRC2_DIR)/toriauxlib/c/toriauxlibcache_component_dat2_convert.c \
	$(SRC2_DIR)/toriauxlib/c/toriauxlibcache_objtype_convert.c \
	$(SRC2_DIR)/toriauxlib/c/toriauxlibcache_font_convert.c \
	$(SRC2_DIR)/vm/cs2_script.c \
	$(SRC2_DIR)/vm/cs2_opcode_meta.c \
	$(SRC_DIR)/3rd/bzip/bzip.c \
	$(SRC_DIR)/3rd/miniz/miniz.c

OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.c=.o)))

all: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

check_cache_revision: check_cache_revision.c $(SRC_DIR)/osrs/rscache/rscache.u.c $(SRC2_DIR)/vm/cs2_script.c $(SRC2_DIR)/vm/cs2_opcode_meta.c $(SRC_DIR)/3rd/bzip/bzip.c $(SRC_DIR)/3rd/miniz/miniz.c
	$(CC) $(CFLAGS) -w -o $@ check_cache_revision.c $(SRC_DIR)/osrs/rscache/rscache.u.c $(SRC2_DIR)/vm/cs2_script.c $(SRC2_DIR)/vm/cs2_opcode_meta.c $(SRC_DIR)/3rd/bzip/bzip.c $(SRC_DIR)/3rd/miniz/miniz.c $(LDFLAGS)

$(OBJ_DIR)/main.o: $(TOOL_DIR)/main.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/bmp.o: $(SRC_DIR)/bmp.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/stubs.o: $(TOOL_DIR)/stubs.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toridraw_unity.o: $(SRC2_DIR)/toridraw/toridraw_unity.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toridraw_model_sprite.o: $(SRC2_DIR)/toridraw/toridraw_model_sprite.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toridraw_cachemodel.o: $(SRC2_DIR)/toriauxlib/td/toridraw_cachemodel.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toriauxlibcore_types.o: $(SRC2_DIR)/toriauxlib/core/toriauxlibcore_types.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toriauxlibcache_clientscript_convert.o: $(SRC2_DIR)/toriauxlib/c/toriauxlibcache_clientscript_convert.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toriauxlibcache_component_dat2_convert.o: $(SRC2_DIR)/toriauxlib/c/toriauxlibcache_component_dat2_convert.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toriauxlibcache_objtype_convert.o: $(SRC2_DIR)/toriauxlib/c/toriauxlibcache_objtype_convert.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toriauxlibcache_font_convert.o: $(SRC2_DIR)/toriauxlib/c/toriauxlibcache_font_convert.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toridraw_font.o: $(SRC2_DIR)/toridraw/toridraw_font.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cs2_script.o: $(SRC2_DIR)/vm/cs2_script.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cs2_opcode_meta.o: $(SRC2_DIR)/vm/cs2_opcode_meta.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/bzip.o: $(SRC_DIR)/3rd/bzip/bzip.c
	$(CC) $(CFLAGS) -w -c $< -o $@

$(OBJ_DIR)/miniz.o: $(SRC_DIR)/3rd/miniz/miniz.c
	$(CC) $(CFLAGS) -w -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean
