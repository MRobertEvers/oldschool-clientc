# ── WASM target ───────────────────────────────────────────────────────────────
EMCC     := emcc
EFLAGS   := -O2 --no-entry
WASM_OUT := filestore.js

# ── Native Lua host target ────────────────────────────────────────────────────
CC       := cc
CFLAGS   := -O2 -Wall -Wextra
HOST     := lua-host

# Detect Lua headers + libs.
# Priority: 1) Homebrew (macOS)  2) pkg-config  3) bare -llua fallback

LUA_CFLAGS :=
LUA_LIBS   :=

# 1. Homebrew – works even when pkg-config is not installed.
ifeq ($(shell uname),Darwin)
    _BREW_LUA := $(shell brew --prefix lua 2>/dev/null)
    ifneq ($(_BREW_LUA),)
        LUA_CFLAGS := -I$(_BREW_LUA)/include/lua
        LUA_LIBS   := -L$(_BREW_LUA)/lib -llua
    endif
endif

# 2. pkg-config – Linux / systems where it is available.
ifeq ($(LUA_CFLAGS),)
    LUA_PC := $(firstword $(foreach p, lua5.4 lua54 lua5.3 lua53 lua-5.4 lua-5.3 lua,\
                  $(if $(shell pkg-config --exists $(p) 2>/dev/null && echo y),$(p),)))
    ifneq ($(LUA_PC),)
        LUA_CFLAGS := $(shell pkg-config --cflags $(LUA_PC))
        LUA_LIBS   := $(shell pkg-config --libs   $(LUA_PC))
    endif
endif

# 3. Bare fallback – headers assumed to be in default include paths.
ifeq ($(LUA_CFLAGS),)
    LUA_LIBS := -llua -lm
endif

# ── Phony targets ─────────────────────────────────────────────────────────────
.PHONY: all wasm host clean

all: wasm host

# ── WASM: compile filestore.c → filestore.js + filestore.wasm ────────────────
wasm: $(WASM_OUT)

$(WASM_OUT): filestore.c
	$(EMCC) $(EFLAGS) $< -o $@ \
		-sMODULARIZE=1 \
		-sEXPORT_NAME=createFilestoreModule \
		-sENVIRONMENT=web \
		"-sEXPORTED_FUNCTIONS=[\"_read_file\"]" \
		"-sEXPORTED_RUNTIME_METHODS=[\"ccall\",\"UTF8ToString\"]"

# ── Native host: compile main.c → lua-host ────────────────────────────────────
host: $(HOST)

# Guard: emit a friendly message if Lua headers cannot be found.
LUA_H := $(shell echo "\#include <lua.h>" | $(CC) -x c - -fsyntax-only $(LUA_CFLAGS) 2>/dev/null && echo ok)

$(HOST): main.c
ifeq ($(LUA_H),ok)
	$(CC) $(CFLAGS) $(LUA_CFLAGS) $< -o $@ $(LUA_LIBS)
else
	@echo ""
	@echo "  ERROR: Lua development headers not found."
	@echo "  Install Lua and try again:"
	@echo "    macOS:   brew install lua"
	@echo "    Ubuntu:  sudo apt install liblua5.4-dev"
	@echo "    Fedora:  sudo dnf install lua-devel"
	@echo "    Alpine:  apk add lua5.4-dev"
	@echo ""
	@exit 1
endif

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -f $(WASM_OUT) filestore.wasm $(HOST)
