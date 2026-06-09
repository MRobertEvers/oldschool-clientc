# Emscripten build → dist/browser.js + dist/browser.wasm, plus dist/index.html
# (copy of public/shell.html), every $(PLAT_DIR)/*.js, and every public/*.js copied into dist/.
#
# After `make`, serve dist/, e.g.:
#   python3 -m http.server -d dist 8080
# Then open http://localhost:8080/ or http://localhost:8080/index.html

EMCC  ?= emcc
REPO_ROOT := ../../..
SRC2  := $(REPO_ROOT)/src2
SRC_DIR := $(REPO_ROOT)/src
SCRIPT_DIR := $(SRC2)/scripting
PLAT_DIR := $(SRC2)/platforms
CMD_DIR := $(SRC2)/commands
INPUT_DIR := $(SRC2)/input
IOQUEUE_DIR := $(SRC2)/ioqueue

SRC   := browser.c \
	$(SRC2)/libtorirs.c \
	$(SCRIPT_DIR)/libtorirs_scripting.c \
	$(SCRIPT_DIR)/libtorirs_scriptapi.c \
	$(CMD_DIR)/libtorirs_command_queue.c \
	$(INPUT_DIR)/libtorirs_input.c \
	$(IOQUEUE_DIR)/libtorirs_ioqueue.c \
	$(SRC2)/render/libtorirs_render.c \
	$(SRC2)/world/world_scene_events.c \
	$(SRC2)/world/world_scene.c \
	$(SRC2)/world/world_scene_platform.c \
	$(SRC2)/world/world.c \
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
	$(SRC2)/gamecache/gamecache.c \
	$(SRC2)/gamecache/gamecache_convert.c \
	$(SRC2)/gamecache/gamecache_submit.c \
	$(SRC2)/gamecache/toridraw_cachemodel.c \
	$(SRC2)/gamecache/toridraw_cachesprite.c \
	$(SRC2)/gamecache/gamecache_l.c \
	$(SRC2)/toridraw/toridraw_animation.c \
	$(SRC2)/toridraw/toridraw_model_transform.c \
	$(SRC2)/games/model_viewer.c \
	$(SRC2)/games/runescape.c \
	$(SRC2)/core/tasks/core_task.c \
	$(SRC2)/toridraw/toridraw.c \
	$(SRC2)/toridraw/toridraw_math.c \
	$(SRC2)/toridraw/toridraw_map.c \
	$(SRC2)/toridraw/toridraw_vec.c \
	$(SRC2)/toridraw/toridraw_hsl16.c \
	$(SRC2)/toridraw/toridraw_lighting.c \
	$(SRC2)/toridraw/toridraw_model.c \
	$(SRC2)/toridraw/toridraw_light_model.c \
	$(SRC2)/toridraw/toridraw_sprite.c \
	$(SRC_DIR)/graphics/dashmap.c \
	$(SRC_DIR)/graphics/lighting.c \
	$(SRC_DIR)/graphics/shared_tables.c \
	$(SRC_DIR)/osrs/palette.c \
	$(SRC_DIR)/osrs/painters.c \
	$(SRC_DIR)/osrs/texture.c \
	$(PLAT_DIR)/trspk_toridraw.c \
	$(PLAT_DIR)/platform_emscripten_jshost.c \
	$(PLAT_DIR)/platform_js_capi.c \
	$(PLAT_DIR)/platform_sdl2/platform_sdl2.c \
	$(PLAT_DIR)/platform_sdl2/platform_sdl2_renderer_webgl1.c \
	$(SRC2)/platformkit/core/trspk_atlas.c \
	$(SRC2)/platformkit/core/trspk_vbo.c \
	$(SRC2)/platformkit/core/trspk_vbochain16.c \
	$(SRC2)/platformkit/core/trspk_modelarena.c \
	$(SRC2)/platformkit/core/trspk_pose.c \
	$(SRC2)/platformkit/core/trspk_ibo.c \
	$(SRC2)/platformkit/core/trspk_triangles.c \
	$(PLAT_DIR)/platform_x/cachelib.c \
	$(PLAT_DIR)/platform_x/cachelib_platform.c \
	$(PLAT_DIR)/platform_x/cachelib_serialized.c \
	$(SRC_DIR)/osrs/rscache/cache.c \
	$(SRC_DIR)/osrs/rscache/cache_dat.c \
	$(SRC_DIR)/osrs/rscache/xtea_config.c \
	$(SRC_DIR)/osrs/rscache/reference_table.c \
	$(SRC_DIR)/osrs/rscache/filelist.c \
	$(SRC_DIR)/osrs/rscache/disk.c \
	$(SRC_DIR)/osrs/rscache/rsbuf.c \
	$(SRC_DIR)/osrs/rscache/archive.c \
	$(SRC_DIR)/osrs/rscache/archive_decompress.c \
	$(SRC_DIR)/osrs/rscache/compression.c \
	$(SRC_DIR)/osrs/rscache/xtea.c \
	$(SRC_DIR)/3rd/bzip/bzip.c \
	$(SRC_DIR)/3rd/miniz/miniz.c \
	$(SRC_DIR)/osrs/rscache/tables/string_utils.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/pix8.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/pix32.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/config_versionlist_mapsquare.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/config_textures.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/config_idk.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/config_npc.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/config_obj.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/config_spotanim.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/config_varp.c \
	$(SRC_DIR)/osrs/rscache/tables_dat/animframe.c \
	$(SRC_DIR)/osrs/rscache/tables/config_sequence.c \
	$(SRC_DIR)/osrs/rscache/tables/config_floortype.c \
	$(SRC_DIR)/osrs/rscache/tables/config_locs.c \
	$(SRC_DIR)/osrs/rscache/tables/maps.c \
	$(SRC_DIR)/osrs/rscache/tables/noise.c \
	$(SRC_DIR)/osrs/rscache/tables/model.c 

OUT   := dist/browser.js

PUBLIC_JS := $(wildcard public/*.js)
PLAT_JS := $(wildcard $(PLAT_DIR)/*.js)
REVS_SCRIPTS := $(SRC2)/revs/scripts

# Release build: make EFLAGS='-O2'
EFLAGS ?= -O0 -g
# WebGL1 renderer: GLES2 context + WebGL 1.0 API (see platform_impl2_sdl2_port_emscripten.cpp).
EMFLAGS := $(EFLAGS) -gsource-map -Wall -Wextra -sENVIRONMENT=web -sUSE_SDL=2 \
	-sMIN_WEBGL_VERSION=1 -sMAX_WEBGL_VERSION=1 -sFULL_ES2=1 \
	-sINITIAL_MEMORY=128MB \
	-I$(SRC2) -I$(SCRIPT_DIR) -I$(PLAT_DIR) -I$(SRC_DIR) -I$(REPO_ROOT) \
	-sEXPORTED_RUNTIME_METHODS=['HEAPU8'] \
	-sFORCE_FILESYSTEM=1 \
	--preload-file $(REVS_SCRIPTS)@/revs/scripts

.PHONY: all clean

all: $(OUT) dist/index.html

$(OUT): $(SRC)
	@command -v $(EMCC) >/dev/null 2>&1 || { \
		echo ""; \
		echo "  ERROR: $(EMCC) not found. Install Emscripten SDK:"; \
		echo "    https://emscripten.org/docs/getting_started/downloads.html"; \
		echo ""; \
		exit 1; \
	}
	@mkdir -p dist
	$(EMCC) $(EMFLAGS) $(SRC) -o $(OUT)

dist/index.html: public/shell.html $(PUBLIC_JS) $(PLAT_JS) $(OUT)
	@mkdir -p dist
	@if [ -n "$(PUBLIC_JS)" ]; then cp $(PUBLIC_JS) dist/; fi
	@if [ -n "$(PLAT_JS)" ]; then cp $(PLAT_JS) dist/; fi
	cp public/shell.html dist/index.html

clean:
	rm -rf dist
