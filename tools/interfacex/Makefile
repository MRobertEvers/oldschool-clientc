CC = clang
OBJ_DIR = build
TARGET = interfacex

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SRC_DIR   := $(REPO_ROOT)/src
SRC2_DIR  := $(REPO_ROOT)/src2
PLAT_DIR  := $(SRC2_DIR)/platforms
TOOL_DIR  := $(REPO_ROOT)/tools/interfacex

UNAME_S := $(shell uname -s 2>/dev/null)
ifeq ($(UNAME_S),Darwin)
  HOST_OS := MacOS
else ifeq ($(UNAME_S),Linux)
  HOST_OS := Linux
else
  HOST_OS := Linux
endif

# AddressSanitizer: default-off on macOS (dyld deadlocks in AsanInitFromRtl before
# main with Apple's dynamic ASan runtime). Use: make asan
ifdef ASAN
  ENABLE_ASAN := 1
endif
ENABLE_ASAN ?= 0

CFLAGS = -std=c11 -Wall -Wextra -g \
	-I$(TOOL_DIR) -I$(SRC2_DIR)/toridraw -I$(SRC2_DIR)/toriauxlib/td -I$(SRC2_DIR) -I$(SRC_DIR) -I$(REPO_ROOT)

LDFLAGS = -lm

ifeq ($(ENABLE_ASAN),1)
  CFLAGS += -fsanitize=address -fno-omit-frame-pointer -DTORIRS_ENABLE_ASAN=1
  LDFLAGS += -fsanitize=address
  ifeq ($(HOST_OS),MacOS)
    CC := clang
    # macOS 26+: interpose dyld_shared_cache_iterate_text before ASan init (LLVM #182943).
    ASAN_DYLD_SHIM := asan_dyld_shim.dylib
  endif
endif

ASAN_DYLD_SHIM ?=

SRCS = $(TOOL_DIR)/main.c \
	$(TOOL_DIR)/interfacex_host_io.c \
	$(TOOL_DIR)/stubs.c \
	$(PLAT_DIR)/asan_compat.c \
	$(SRC_DIR)/bmp.c \
	$(SRC2_DIR)/toridraw/toridraw_unity.c \
	$(SRC2_DIR)/toridraw/toridraw_model_sprite.c \
	$(SRC2_DIR)/toridraw/toridraw_font.c \
	$(SRC2_DIR)/toriauxlib/td/toridraw_cachemodel.c \
	$(SRC2_DIR)/toriauxlib/td/toridraw_cachesprite.c \
	$(SRC2_DIR)/toriauxlib/core/toriauxlibcore.c \
	$(SRC2_DIR)/toriauxlib/core/toriauxlibcore_types.c \
	$(SRC2_DIR)/toriauxlib/cache/toriauxlibcache.c \
	$(SRC2_DIR)/toriauxlib/cache/toriauxlibcache_convert.c \
	$(SRC2_DIR)/toriauxlib/cache/toriauxlibcache_model_convert.c \
	$(SRC2_DIR)/toriauxlib/cache/toriauxlibcache_submit.c \
	$(SRC2_DIR)/toriauxlib/cache/toriauxlibcache_sprite_convert.c \
	$(SRC2_DIR)/toriauxlib/cache/toriauxlibcache_clientscript_convert.c \
	$(SRC2_DIR)/toriauxlib/cache/toriauxlibcache_component_dat2_convert.c \
	$(SRC2_DIR)/toriauxlib/cache/toriauxlibcache_font_convert.c \
	$(SRC2_DIR)/buildcache/dat1_buildcache.c \
	$(SRC2_DIR)/buildcache/dat2_buildcache.c \
	$(SRC2_DIR)/buildcache/dat2_buildcache_meta.c \
	$(SRC2_DIR)/buildcache/dat2_buildcache_ui.c \
	$(SRC2_DIR)/games/ie_enum_lookup.c \
	$(SRC2_DIR)/games/ie_struct_lookup.c \
	$(SRC2_DIR)/games/ie_param_lookup.c \
	$(SRC2_DIR)/toriauxlib/core/tasks/libtori_core_task.c \
	$(SRC2_DIR)/toriauxlib/core/tasks/libtori_core_task_runner.c \
	$(SRC2_DIR)/toriauxlib/core/tasks/task_clientscript_load.c \
	$(SRC2_DIR)/toriauxlib/core/tasks/task_dat2_config_entry_load.c \
	$(SRC2_DIR)/toriauxlib/core/tasks/task_dat2_anim_io.c \
	$(SRC2_DIR)/runescape/appearance.c \
	$(SRC_DIR)/osrs/varp_varbit_manager.c \
	$(SRC2_DIR)/platforms/platform_x_io_reactor.c \
	$(SRC2_DIR)/platforms/platform_x/cachelib.c \
	$(SRC2_DIR)/platforms/platform_x/cachelib_platform.c \
	$(SRC2_DIR)/platforms/platform_x/cachelib_serialized.c \
	$(SRC2_DIR)/vm/cs2_script.c \
	$(SRC2_DIR)/vm/cs2_opcode_meta.c \
	$(SRC_DIR)/3rd/bzip/bzip.c \
	$(SRC_DIR)/3rd/miniz/miniz.c

OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.c=.o)))

all: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

asan_dyld_shim.dylib: $(PLAT_DIR)/asan_dyld_shim.c
	$(CC) -dynamiclib -o $@ $< -DTORIRS_ENABLE_ASAN=1

$(TARGET): $(OBJS) $(ASAN_DYLD_SHIM)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(ASAN_DYLD_SHIM)

check_cache_revision: check_cache_revision.c $(SRC_DIR)/osrs/rscache/rscache_unity.h $(SRC2_DIR)/vm/cs2_script.c $(SRC2_DIR)/vm/cs2_opcode_meta.c $(SRC_DIR)/3rd/bzip/bzip.c $(SRC_DIR)/3rd/miniz/miniz.c
	$(CC) $(CFLAGS) -w -o $@ check_cache_revision.c -include $(SRC_DIR)/osrs/rscache/rscache_unity.h $(SRC2_DIR)/vm/cs2_script.c $(SRC2_DIR)/vm/cs2_opcode_meta.c $(SRC_DIR)/3rd/bzip/bzip.c $(SRC_DIR)/3rd/miniz/miniz.c $(LDFLAGS)

$(OBJ_DIR)/main.o: $(TOOL_DIR)/main.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/interfacex_host_io.o: $(TOOL_DIR)/interfacex_host_io.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/bmp.o: $(SRC_DIR)/bmp.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/stubs.o: $(TOOL_DIR)/stubs.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/asan_compat.o: $(PLAT_DIR)/asan_compat.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toridraw_unity.o: $(SRC2_DIR)/toridraw/toridraw_unity.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toridraw_model_sprite.o: $(SRC2_DIR)/toridraw/toridraw_model_sprite.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toridraw_cachemodel.o: $(SRC2_DIR)/toriauxlib/td/toridraw_cachemodel.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toridraw_cachesprite.o: $(SRC2_DIR)/toriauxlib/td/toridraw_cachesprite.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toriauxlibcore.o: $(SRC2_DIR)/toriauxlib/core/toriauxlibcore.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toriauxlibcore_types.o: $(SRC2_DIR)/toriauxlib/core/toriauxlibcore_types.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toriauxlibcache.o: $(SRC2_DIR)/toriauxlib/cache/toriauxlibcache.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toriauxlibcache_convert.o: $(SRC2_DIR)/toriauxlib/cache/toriauxlibcache_convert.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toriauxlibcache_model_convert.o: $(SRC2_DIR)/toriauxlib/cache/toriauxlibcache_model_convert.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toriauxlibcache_submit.o: $(SRC2_DIR)/toriauxlib/cache/toriauxlibcache_submit.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toriauxlibcache_sprite_convert.o: $(SRC2_DIR)/toriauxlib/cache/toriauxlibcache_sprite_convert.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toriauxlibcache_clientscript_convert.o: $(SRC2_DIR)/toriauxlib/cache/toriauxlibcache_clientscript_convert.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toriauxlibcache_component_dat2_convert.o: $(SRC2_DIR)/toriauxlib/cache/toriauxlibcache_component_dat2_convert.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toriauxlibcache_font_convert.o: $(SRC2_DIR)/toriauxlib/cache/toriauxlibcache_font_convert.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/dat1_buildcache.o: $(SRC2_DIR)/buildcache/dat1_buildcache.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/dat2_buildcache.o: $(SRC2_DIR)/buildcache/dat2_buildcache.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/dat2_buildcache_meta.o: $(SRC2_DIR)/buildcache/dat2_buildcache_meta.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/dat2_buildcache_ui.o: $(SRC2_DIR)/buildcache/dat2_buildcache_ui.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/ie_enum_lookup.o: $(SRC2_DIR)/games/ie_enum_lookup.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/ie_struct_lookup.o: $(SRC2_DIR)/games/ie_struct_lookup.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/ie_param_lookup.o: $(SRC2_DIR)/games/ie_param_lookup.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/libtori_core_task.o: $(SRC2_DIR)/toriauxlib/core/tasks/libtori_core_task.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/libtori_core_task_runner.o: $(SRC2_DIR)/toriauxlib/core/tasks/libtori_core_task_runner.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/task_clientscript_load.o: $(SRC2_DIR)/toriauxlib/core/tasks/task_clientscript_load.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/task_dat2_config_entry_load.o: $(SRC2_DIR)/toriauxlib/core/tasks/task_dat2_config_entry_load.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/task_dat2_anim_io.o: $(SRC2_DIR)/toriauxlib/core/tasks/task_dat2_anim_io.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/appearance.o: $(SRC2_DIR)/runescape/appearance.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/varp_varbit_manager.o: $(SRC_DIR)/osrs/varp_varbit_manager.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/platform_x_io_reactor.o: $(SRC2_DIR)/platforms/platform_x_io_reactor.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cachelib.o: $(SRC2_DIR)/platforms/platform_x/cachelib.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cachelib_platform.o: $(SRC2_DIR)/platforms/platform_x/cachelib_platform.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cachelib_serialized.o: $(SRC2_DIR)/platforms/platform_x/cachelib_serialized.c
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
	rm -rf $(OBJ_DIR) $(TARGET) asan_dyld_shim.dylib

asan:
	$(MAKE) ENABLE_ASAN=1 clean
	$(MAKE) ENABLE_ASAN=1 all

.PHONY: all clean asan
