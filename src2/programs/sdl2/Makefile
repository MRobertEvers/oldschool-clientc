# Build tiny host that exercises platform_x_lua (sdl2.c name is historical).

REPO_ROOT := ../../..
SRC_DIR   := $(REPO_ROOT)/src
SRC2      := $(REPO_ROOT)/src2
PLAT_DIR  := $(SRC2)/platforms
SCRIPT_DIR := $(SRC2)/scripting

OBJ_DIR := sdl2_build
PROG    := sdl2

CC      ?= cc
CFLAGS  ?= -O0 -Wall -Wextra -g
CPPFLAGS += -I$(SRC_DIR) -I$(SRC2) -I$(SCRIPT_DIR) 

# Vendored Lua — embed library, not lua/luac interpreters.
LUA_SRC := $(filter-out $(SRC_DIR)/3rd/lua/lua.c $(SRC_DIR)/3rd/lua/luac.c,\
	$(wildcard $(SRC_DIR)/3rd/lua/*.c))

SRCS := sdl2.c \
	$(PLAT_DIR)/platform_x_lua.c \
	$(PLAT_DIR)/platform_sdl2.c \
	$(SCRIPT_DIR)/libtorirs_scripting.c \
	$(SRC2)/commands/libtorirs_command_queue.c \
	$(SRC2)/input/libtorirs_input.c \
	$(SRC2)/libtorirs.c \
	$(LUA_SRC)

SDL_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
SDL_LIBS := $(shell pkg-config --libs sdl2 2>/dev/null)
ifeq ($(strip $(SDL_LIBS)),)
SDL_CFLAGS := $(shell sdl2-config --cflags 2>/dev/null)
SDL_LIBS := $(shell sdl2-config --libs 2>/dev/null)
endif
ifeq ($(strip $(SDL_LIBS)),)
ifeq ($(shell uname -s),Darwin)
SDL_CFLAGS := -I/opt/homebrew/include/SDL2 -I/usr/local/include/SDL2
SDL_LIBS := -L/opt/homebrew/lib -L/usr/local/lib -lSDL2
else
SDL_CFLAGS := -I/usr/include/SDL2
SDL_LIBS := -lSDL2
endif
endif

# Flat object list in OBJ_DIR (same pattern as src/osrs/revconfig/Makefile)
OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.c=.o)))

.PHONY: all clean

all: $(OBJ_DIR) $(PROG)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(SDL_LIBS) -lm

define make-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SDL_CFLAGS) -c $(1) -o $$@
endef

$(foreach src,$(SRCS),$(eval $(call make-goal,$(src))))

clean:
	rm -f $(PROG)
	rm -rf $(OBJ_DIR)
