# Emscripten build → dist/browser.js + dist/browser.wasm, plus dist/index.html
# (copy of public/shell.html), every $(PLAT_DIR)/*.js, and every public/*.js copied into dist/.
#
# After `make`, serve dist/, e.g.:
#   python3 -m http.server -d dist 8080
# Then open http://localhost:8080/ or http://localhost:8080/index.html

EMCC  ?= emcc
REPO_ROOT := ../../..
SRC2  := $(REPO_ROOT)/src2
include $(SRC2)/toriauxlib/core/tasks/tasks_sources.mk
SRC_DIR := $(REPO_ROOT)/src
SCRIPT_DIR := $(SRC2)/scripting
PLAT_DIR := $(SRC2)/platforms
CMD_DIR := $(SRC2)/commands
INPUT_DIR := $(SRC2)/input
IOQUEUE_DIR := $(SRC2)/ioqueue
LVGL_DIR := $(SRC2)/3rd/lvgl
LVGL_OBJ_DIR := browser_build/lvgl

SRC   := browser.c \
	$(SRC2)/libtorirs.c \
	$(SCRIPT_DIR)/libtorirs_scripting.c \
	$(SCRIPT_DIR)/libtorirs_scriptapi.c \
	$(CMD_DIR)/libtorirs_command_queue.c \
	$(INPUT_DIR)/libtorirs_input.c \
	$(SRC2)/render/libtorirs_render.c \
	$(SRC2)/toridraw/toridraw_scene.c \
	$(SRC2)/toridraw/toridraw_intrusive_list.c \
	$(SRC2)/world/world.c \
	$(SRC2)/world/world_entity.c \
	$(SRC2)/world/world_pickset.c \
	$(SRC2)/world/world_builder.c \
	$(SRC2)/world/world_decode_tile.c \
	$(SRC2)/world/collision_map.c \
	$(SRC2)/world/heightmap.c \
	$(SRC2)/world/lightmap.c \
	$(SRC2)/world/sharelight_map.c \
	$(SRC2)/world/shademap.c \
	$(SRC2)/world/blendmap.c \
	$(SRC2)/world/overlaymap.c \
	$(SRC2)/world/decor_buildmap.c \
	$(SRC2)/world/minimap.c \
	$(SRC2)/world/terrain_shapemap.c \
	$(SRC2)/world/contour_ground.c \
	$(SRC2)/buildcache/dat1_buildcache.c \
	$(SRC2)/buildcache/dat2_buildcache.c \
	$(SRC2)/toriauxlib/toriauxlib.c \
	$(SRC2)/toriauxlib/core/toriauxlibcore.c \
	$(SRC2)/toriauxlib/core/toriauxlibcore_types.c \
	$(SRC2)/toriauxlib/cache/toriauxlibcache.c \
	$(SRC2)/toriauxlib/cache/toriauxlibcache_convert.c \
	$(SRC2)/toriauxlib/cache/toriauxlibcache_model_convert.c \
	$(SRC2)/toriauxlib/cache/toriauxlibcache_sprite_convert.c \
	$(SRC2)/toriauxlib/cache/toriauxlibcache_font_convert.c \
	$(SRC2)/toriauxlib/cache/toriauxlibcache_clientscript_convert.c \
	$(SRC2)/toriauxlib/cache/toriauxlibcache_submit.c \
	$(SRC2)/toriauxlib/td/toriauxlibtd.c \
	$(SRC2)/toriauxlib/td/toridraw_cachesprite.c \
	$(SRC2)/toriauxlib/td/toridraw_cachemodel.c \
	$(SRC2)/vm/cs1vm.c \
	$(SRC2)/vm/cs1vm_host.c \
	$(SRC2)/vm/cs1vm_level.c \
	$(SRC2)/vm/cs2vm_unity.c \
	$(SRC2)/toriauxlib/vm/toriauxlibvm.c \
	$(SRC_DIR)/osrs/varp_varbit_manager.c \
	$(SRC2)/toridraw/toridraw_animation.c \
	$(SRC2)/toridraw/toridraw_model_transform.c \
	$(SRC2)/games/model_viewer.c \
	$(SRC2)/games/runescape.c \
	$(SRC2)/runescape/appearance.c \
	$(SRC2)/runescape/player_body.c \
	$(SRC_DIR)/osrs/datatypes/appearances.c \
	$(TORIAUXLIB_TASK_SOURCES) \
	$(SRC2)/toridraw/toridraw.c \
	$(SRC2)/toridraw/toridraw_math.c \
	$(SRC2)/toridraw/toridraw_map.c \
	$(SRC2)/toridraw/toridraw_vec.c \
	$(SRC2)/toridraw/toridraw_hsl16.c \
	$(SRC2)/toridraw/toridraw_lighting.c \
	$(SRC2)/toridraw/toridraw_model.c \
	$(SRC2)/toridraw/toridraw_light_model.c \
	$(SRC2)/toridraw/toridraw_sprite.c \
	$(SRC2)/toridraw/toridraw_model_sprite.c \
	$(SRC2)/toridraw/toridraw_font.c \
	$(SRC2)/buildcache/dat1_buildcache_ui.c \
	$(SRC2)/buildcache/dat2_buildcache_ui.c \
	$(SRC2)/ui/ui_sprite_lookup.c \
	$(SRC2)/ui/ui_font_lookup.c \
	$(SRC2)/ui/interaction_state.c \
	$(SRC2)/ui/ui_minimenu.c \
	$(SRC2)/ui/minimenu_pickset.c \
	$(SRC2)/ui/ui_click.c \
	$(SRC2)/ui/ui_chat_minimenu.c \
	$(SRC2)/ui/ui_debug.c \
	$(SRC2)/ui/uitree.c \
	$(SRC2)/ui/uitree_layout.c \
	$(SRC2)/ui/uitree_host.c \
	$(SRC2)/ui/ui_input.c \
	$(SRC2)/ui/ui_scroll.c \
	$(SRC2)/ui/ui_scroll_runtime.c \
	$(SRC2)/ui/ui_cross_cursor.c \
	$(SRC2)/ui/ui_hover_routing.c \
	$(SRC2)/ui/ui_inv_selection.c \
	$(SRC2)/ui/chat_state.c \
	$(SRC2)/world/entity_registry.c \
	$(SRC2)/ui/rs_inv_container.c \
	$(SRC2)/ui/ui_inv_data_service.c \
	$(SRC2)/ui/ui_inv_slot_view.c \
	$(SRC2)/ui/ui_behavior.c \
	$(SRC2)/ui/ui_input_adapter.c \
	$(SRC2)/3rd/ini/ini.c \
	$(SRC2)/revconfig/revconfig.c \
	$(SRC2)/revconfig/revconfig_load.c \
	$(SRC_DIR)/graphics/dashmap.c \
	$(SRC_DIR)/graphics/lighting.c \
	$(SRC2)/toridraw/graphics/shared_tables.c \
	$(SRC_DIR)/osrs/palette.c \
	$(SRC_DIR)/osrs/painters.c \
	$(SRC_DIR)/osrs/texture.c \
	$(PLAT_DIR)/trspk_toridraw.c \
	$(PLAT_DIR)/trspk_sprite.c \
	$(PLAT_DIR)/platform_emscripten_jshost.c \
	$(PLAT_DIR)/platform_js_capi.c \
	$(PLAT_DIR)/platform_sdl2/platform_sdl2.c \
	$(PLAT_DIR)/platform_sdl2/platform_sdl2_lvgl_hud.c \
	$(PLAT_DIR)/platform_sdl2/platform_sdl2_renderer_soft3d.c \
	$(PLAT_DIR)/platform_sdl2/platform_sdl2_renderer_webgl1.c \
	$(SRC2)/platformkit/core/trspk_atlas.c \
	$(SRC2)/platformkit/core/trspk_core_math.c \
	$(SRC2)/platformkit/core/trspk_vbo.c \
	$(SRC2)/platformkit/core/trspk_vbochain16.c \
	$(SRC2)/platformkit/core/trspk_modelarena.c \
	$(SRC2)/platformkit/core/trspk_pose.c \
	$(SRC2)/platformkit/core/trspk_ibo.c \
	$(SRC2)/platformkit/core/trspk_triangles.c \
	$(PLAT_DIR)/platform_x/cachelib.c \
	$(PLAT_DIR)/platform_x/cachelib_platform.c \
	$(PLAT_DIR)/platform_x/cachelib_serialized.c \
	$(SRC_DIR)/osrs/rscache/unity.c \
	$(SRC_DIR)/3rd/bzip/bzip.c \
	$(SRC_DIR)/3rd/miniz/miniz.c

LVGL_SRCS := $(shell find $(LVGL_DIR)/src -name '*.c')
LVGL_OBJS := $(patsubst $(LVGL_DIR)/%.c,$(LVGL_OBJ_DIR)/%.o,$(LVGL_SRCS))

OUT   := dist/browser.js

PUBLIC_JS := $(wildcard public/*.js)
PLAT_JS := $(wildcard $(PLAT_DIR)/*.js)
REVS_SCRIPTS := $(SRC2)/revs/scripts

# Release build: make EFLAGS='-O2'
EFLAGS ?= -O0 -g
MEMTRACE ?= 0

ifeq ($(MEMTRACE),1)
SRC += $(PLAT_DIR)/torirs_memtrace.c
EXPORTED_RUNTIME_METHODS := ['HEAPU8','FS','FS_readFile']
MEMTRACE_EMFLAGS := -DTORIRS_MEMTRACE=1 \
	-fno-builtin-malloc -fno-builtin-calloc -fno-builtin-realloc -fno-builtin-free \
	-Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free
else
EXPORTED_RUNTIME_METHODS := ['HEAPU8']
MEMTRACE_EMFLAGS :=
endif

# WebGL1 renderer: GLES2 context + WebGL 1.0 API (see platform_impl2_sdl2_port_emscripten.cpp).
EMFLAGS := $(EFLAGS) -gsource-map -Wall -Wextra -sENVIRONMENT=web -sUSE_SDL=2 \
	-sMIN_WEBGL_VERSION=1 -sMAX_WEBGL_VERSION=1 -sFULL_ES2=1 \
	-sINITIAL_MEMORY=256MB \
	-DTORIRS_ENABLE_LVGL_HUD=1 -DLV_CONF_INCLUDE_SIMPLE \
	-I$(SRC2)/toridraw -I$(SRC2) -I$(SRC2)/3rd -I$(LVGL_DIR) \
	-I$(SCRIPT_DIR) -I$(PLAT_DIR) -I$(SRC_DIR) -I$(REPO_ROOT) \
	-sEXPORTED_RUNTIME_METHODS=$(EXPORTED_RUNTIME_METHODS) \
	-sFORCE_FILESYSTEM=1 \
	--preload-file $(REVS_SCRIPTS)@/revs/scripts \
	$(MEMTRACE_EMFLAGS)

MEMTRACE_VIEWER := ../../../tools/memtrace/viewer.html

.PHONY: all clean

all: $(OUT) dist/index.html

$(LVGL_OBJ_DIR)/%.o: $(LVGL_DIR)/%.c
	@mkdir -p $(dir $@)
	$(EMCC) $(EMFLAGS) -w -c $< -o $@

$(OUT): $(SRC) $(LVGL_OBJS)
	@command -v $(EMCC) >/dev/null 2>&1 || { \
		echo ""; \
		echo "  ERROR: $(EMCC) not found. Install Emscripten SDK:"; \
		echo "    https://emscripten.org/docs/getting_started/downloads.html"; \
		echo ""; \
		exit 1; \
	}
	@mkdir -p dist
	$(EMCC) $(EMFLAGS) $(SRC) $(LVGL_OBJS) -o $(OUT)

dist/index.html: public/shell.html $(PUBLIC_JS) $(PLAT_JS) $(OUT)
	@mkdir -p dist
	@if [ -n "$(PUBLIC_JS)" ]; then cp $(PUBLIC_JS) dist/; fi
	@if [ -n "$(PLAT_JS)" ]; then cp $(PLAT_JS) dist/; fi
	cp public/shell.html dist/index.html
ifeq ($(MEMTRACE),1)
	cp $(MEMTRACE_VIEWER) dist/memtrace_viewer.html
endif

clean:
	rm -rf dist browser_build
