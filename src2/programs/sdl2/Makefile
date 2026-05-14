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
	$(SRC2)/LibToriRS.c \
	$(SCRIPT_DIR)/libtorirs_scripting.c \
	$(LUA_SRC)

# Flat object list in OBJ_DIR (same pattern as src/osrs/revconfig/Makefile)
OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.c=.o)))

.PHONY: all clean

all: $(OBJ_DIR) $(PROG)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(PROG): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) -lm

define make-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $(1) -o $$@
endef

$(foreach src,$(SRCS),$(eval $(call make-goal,$(src))))

clean:
	rm -f $(PROG)
	rm -rf $(OBJ_DIR)
