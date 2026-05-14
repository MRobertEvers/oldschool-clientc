# Emscripten build → dist/browser.js + dist/browser.wasm, plus dist/index.html
# (copy of public/shell.html) and every public/*.js copied into dist/.
#
# After `make`, serve dist/, e.g.:
#   python3 -m http.server -d dist 8080
# Then open http://localhost:8080/ or http://localhost:8080/index.html

EMCC  ?= emcc
SRC2  := ../..
SCRIPT_DIR := $(SRC2)/scripting
PLAT_DIR := $(SRC2)/platforms
SRC   := browser.c \
	$(SRC2)/libtorirs.c \
	$(SCRIPT_DIR)/libtorirs_scripting.c \
	$(PLAT_DIR)/platform_emscripten_jshost.c
OUT   := dist/browser.js

PUBLIC_JS := $(wildcard public/*.js)

# Override for debug: make EFLAGS='-O0 -g'
EFLAGS ?= -O2
EMFLAGS := $(EFLAGS) -Wall -Wextra -sENVIRONMENT=web -I$(SRC2) -I$(SCRIPT_DIR) \
	-sEXPORTED_RUNTIME_METHODS=['HEAPU8']

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

dist/index.html: public/shell.html $(PUBLIC_JS) $(OUT)
	@mkdir -p dist
	@if [ -n "$(PUBLIC_JS)" ]; then cp $(PUBLIC_JS) dist/; fi
	cp public/shell.html dist/index.html

clean:
	rm -rf dist
