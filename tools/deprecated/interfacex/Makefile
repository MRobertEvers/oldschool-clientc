CC = clang
OBJ_DIR = build
TARGET = interfacex

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SRC_DIR   := $(REPO_ROOT)/v0
SRC2_DIR  := $(REPO_ROOT)/v1
BMP_DIR   := $(REPO_ROOT)/3rd/bmp
RSCACHE_DIR := $(REPO_ROOT)/3rd/rscache
BZIP_DIR    := $(REPO_ROOT)/3rd/bzip
MINIZ_DIR   := $(REPO_ROOT)/3rd/miniz
XTEAS_DIR   := $(REPO_ROOT)/3rd/xteas
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

CFLAGS = -std=c11 -Wall -Wextra -g -DTORIAUXLIBCACHE_NO_LEGACY_TASK_HEADERS \
	-I$(TOOL_DIR) -I$(SRC2_DIR)/toridraw -I$(SRC2_DIR)/toriauxlib/td -I$(SRC2_DIR) -I$(SRC_DIR) -I$(REPO_ROOT) -I$(BMP_DIR) \
	-I$(RSCACHE_DIR)/include -I$(RSCACHE_DIR)/src

RSCACHE_CFLAGS = -std=c11 -g -w \
	-I$(RSCACHE_DIR)/include -I$(RSCACHE_DIR)/src -I$(RSCACHE_DIR) \
	-I$(BZIP_DIR) -I$(MINIZ_DIR) -I$(XTEAS_DIR)

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

TASKS_DIR := $(SRC2_DIR)/toriauxlib/core/tasks
DAT1_TASKS_DIR := $(TASKS_DIR)/dat1
DAT2_TASKS_DIR := $(TASKS_DIR)/dat2

TASK_SRCS = $(TASKS_DIR)/task_cache_wrappers.c \
	$(DAT1_TASKS_DIR)/task_dat1_animate.c \
	$(DAT1_TASKS_DIR)/task_dat1_model_load.c \
	$(DAT1_TASKS_DIR)/task_dat1_npc_add.c \
	$(DAT1_TASKS_DIR)/task_dat1_player_add.c \
	$(DAT1_TASKS_DIR)/task_dat1_projectile_add.c \
	$(DAT1_TASKS_DIR)/task_dat1_world_rebuild_centerzone.c \
	$(DAT1_TASKS_DIR)/task_dat1_world_rebuild_chunk_list.c \
	$(DAT1_TASKS_DIR)/task_dat1_world_rebuild_core.c \
	$(DAT2_TASKS_DIR)/task_dat2_animate.c \
	$(DAT2_TASKS_DIR)/task_dat2_anim_resolve.c \
	$(DAT2_TASKS_DIR)/task_dat2_clientscript_load.c \
	$(DAT2_TASKS_DIR)/task_dat2_config_entry_load.c \
	$(DAT2_TASKS_DIR)/task_dat2_model_load.c \
	$(DAT2_TASKS_DIR)/task_dat2_npc_add.c \
	$(DAT2_TASKS_DIR)/task_dat2_player_add.c \
	$(DAT2_TASKS_DIR)/task_dat2_projectile_add.c \
	$(DAT2_TASKS_DIR)/task_dat2_textures_load.c \
	$(DAT2_TASKS_DIR)/task_dat2_world_rebuild_centerzone.c \
	$(DAT2_TASKS_DIR)/task_dat2_world_rebuild_chunk_list.c \
	$(DAT2_TASKS_DIR)/task_dat2_world_rebuild_core.c

SRCS = $(TOOL_DIR)/main.c \
	$(TOOL_DIR)/interfacex_host_io.c \
	$(TOOL_DIR)/stubs.c \
	$(PLAT_DIR)/asan_compat.c \
	$(BMP_DIR)/bmp.c \
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
	$(TASK_SRCS) \
	$(SRC2_DIR)/runescape/appearance.c \
	$(SRC_DIR)/osrs/varp_varbit_manager.c \
	$(SRC2_DIR)/platforms/platform_x_io_reactor.c \
	$(SRC2_DIR)/platforms/platform_x/cachelib.c \
	$(SRC2_DIR)/platforms/platform_x/cachelib_platform.c \
	$(SRC2_DIR)/platforms/platform_x/cachelib_serialized.c \
	$(SRC2_DIR)/vm/cs2_script.c \
	$(SRC2_DIR)/vm/cs2_opcode_meta.c \
	$(SRC2_DIR)/vm/cs2vmx.c \
	$(TOOL_DIR)/rscache_component_unity.c

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

$(OBJ_DIR)/bmp.o: $(BMP_DIR)/bmp.c
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

define make-task-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1)
	$(CC) $(CFLAGS) -c $(1) -o $$@
endef

$(foreach src,$(TASK_SRCS),$(eval $(call make-task-goal,$(src))))

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

$(OBJ_DIR)/cs2vmx.o: $(SRC2_DIR)/vm/cs2vmx.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/rscache_component_unity.o: $(TOOL_DIR)/rscache_component_unity.c
	$(CC) $(RSCACHE_CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET) asan_dyld_shim.dylib

asan:
	$(MAKE) ENABLE_ASAN=1 clean
	$(MAKE) ENABLE_ASAN=1 all

.PHONY: all clean asan
