# Emscripten build → dist/browser.js + dist/browser.wasm, plus dist/index.html
# (copy of public/shell.html), $(PLAT_DIR)/LibToriPlatformJS.js, and every public/*.js
# copied into dist/.
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
	$(PLAT_DIR)/platform_emscripten_jshost.c \
	$(PLAT_DIR)/platform_js_capi.c \
	$(PLAT_DIR)/platform_sdl2.c \
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
	$(SRC_DIR)/osrs/rscache/tables/model.c

OUT   := dist/browser.js

PUBLIC_JS := $(wildcard public/*.js)
LIBTORI_PLATFORM_JS := $(PLAT_DIR)/LibToriPlatformJS.js
REVS_SCRIPTS := $(SRC2)/revs/scripts

# Override for debug: make EFLAGS='-O0 -g'
EFLAGS ?= -O2
EMFLAGS := $(EFLAGS) -Wall -Wextra -sENVIRONMENT=web -sUSE_SDL=2 \
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

dist/index.html: public/shell.html $(PUBLIC_JS) $(LIBTORI_PLATFORM_JS) $(OUT)
	@mkdir -p dist
	@if [ -n "$(PUBLIC_JS)" ]; then cp $(PUBLIC_JS) dist/; fi
	cp $(LIBTORI_PLATFORM_JS) dist/
	cp public/shell.html dist/index.html

clean:
	rm -rf dist
