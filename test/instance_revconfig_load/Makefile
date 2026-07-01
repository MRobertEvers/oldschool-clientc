# From repo root: make -C test/instance_revconfig_load

CC ?= cc
TEST_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
OBJ_DIR = build
TARGET = instance_revconfig_load_test

REPO_ROOT := $(abspath $(TEST_DIR)/../..)
SRC_DIR   := $(REPO_ROOT)/src
SRC2_DIR  := $(REPO_ROOT)/src2

CFLAGS = -std=c11 -Wall -Wextra -g -MMD -MP \
	-I$(TEST_DIR) -I$(SRC_DIR) -I$(SRC_DIR)/graphics -I$(SRC2_DIR) -I$(SRC2_DIR)/core \
	-I$(SRC2_DIR)/toridraw -I$(SRC2_DIR)/platforms \
	-I$(SRC2_DIR)/toriauxlib/core/tasks -I$(REPO_ROOT)

LDFLAGS = -Wl,-dead_strip -lm

SRCS = $(TEST_DIR)/main.c \
	$(TEST_DIR)/stubs.c \
	$(SRC2_DIR)/ioqueue/libtorirs_ioqueue.c \
	$(SRC2_DIR)/platforms/platform_x_io_reactor.c \
	$(SRC2_DIR)/platforms/platform_x/cachelib.c \
	$(SRC2_DIR)/platforms/platform_x/cachelib_platform.c \
	$(SRC2_DIR)/platforms/platform_x/cachelib_serialized.c \
	$(SRC2_DIR)/platforms/platform_x_cache.c \
	$(SRC2_DIR)/toriauxlib/core/tasks/core_task.c \
	$(SRC2_DIR)/toriauxlib/toriauxlib.c \
	$(SRC2_DIR)/toriauxlib/core/toriauxlibcore.c \
	$(SRC2_DIR)/toriauxlib/core/toriauxlibcore_types.c \
	$(SRC2_DIR)/toriauxlib/c/toriauxlibcache.c \
	$(SRC2_DIR)/toriauxlib/c/toriauxlibcache_convert.c \
	$(SRC2_DIR)/toriauxlib/c/toriauxlibcache_model_convert.c \
	$(SRC2_DIR)/toriauxlib/c/toriauxlibcache_sprite_convert.c \
	$(SRC2_DIR)/toriauxlib/c/toriauxlibcache_font_convert.c \
	$(SRC2_DIR)/toriauxlib/c/toriauxlibcache_clientscript_convert.c \
	$(SRC2_DIR)/toriauxlib/c/toriauxlibcache_submit.c \
	$(SRC2_DIR)/toriauxlib/td/toridraw_cachesprite.c \
	$(SRC2_DIR)/buildcache/dat1_buildcache.c \
	$(SRC2_DIR)/buildcache/dat2_buildcache.c \
	$(SRC2_DIR)/buildcache/dat1_buildcache_ui.c \
	$(SRC2_DIR)/buildcache/dat2_buildcache_ui.c \
	$(SRC2_DIR)/ui/ui_sprite_lookup.c \
	$(SRC2_DIR)/ui/ui_font_lookup.c \
	$(SRC2_DIR)/ui/ui_input.c \
	$(SRC2_DIR)/ui/ui_scroll.c \
	$(SRC2_DIR)/ui/ui_behavior.c \
	$(SRC2_DIR)/ui/uitree_host.c \
	$(SRC2_DIR)/ui/ui_click.c \
	$(SRC2_DIR)/ui/ui_chat_minimenu.c \
	$(SRC2_DIR)/ui/ui_debug.c \
	$(SRC2_DIR)/ui/ui_minimenu.c \
	$(SRC2_DIR)/ui/interaction_state.c \
	$(SRC2_DIR)/ui/minimenu_pickset.c \
	$(SRC2_DIR)/ui/uitree.c \
	$(SRC2_DIR)/ui/uitree_layout.c \
	$(SRC2_DIR)/vm/csvm.c \
	$(SRC2_DIR)/vm/cs2vm.c \
	$(SRC2_DIR)/toriauxlib/core/tasks/instance_revconfig_context.c \
	$(SRC2_DIR)/toriauxlib/core/tasks/task_rs_component_load.c \
	$(SRC2_DIR)/toriauxlib/core/tasks/task_rs_inv_load.c \
	$(SRC2_DIR)/toriauxlib/core/tasks/task_instance_on_rc.c \
	$(SRC2_DIR)/toriauxlib/core/tasks/task_instance_revconfig_load.c \
	$(TEST_DIR)/font_td_shim.c \
	$(SRC2_DIR)/toridraw/toridraw_scene.c \
	$(SRC2_DIR)/toridraw/toridraw_intrusive_list.c \
	$(SRC2_DIR)/toridraw/toridraw_sprite.c \
	$(SRC2_DIR)/toridraw/toridraw_font.c \
	$(SRC2_DIR)/toridraw/toridraw_math.c \
	$(SRC2_DIR)/toridraw/toridraw_map.c \
	$(SRC2_DIR)/toridraw/toridraw_vec.c \
	$(SRC2_DIR)/commands/libtorirs_command_queue.c \
	$(SRC2_DIR)/revconfig/revconfig.c \
	$(SRC2_DIR)/revconfig/revconfig_load.c \
	$(SRC_DIR)/osrs/varp_varbit_manager.c \
	$(SRC2_DIR)/world/minimap.c \
	$(SRC2_DIR)/world/world.c \
	$(SRC2_DIR)/world/world_entity.c \
	$(SRC2_DIR)/world/world_pickset.c \
	$(SRC2_DIR)/3rd/ini/ini.c \
	$(SRC_DIR)/osrs/rscache/unity.c \
	$(SRC_DIR)/3rd/bzip/bzip.c \
	$(SRC_DIR)/3rd/miniz/miniz.c \
	$(SRC_DIR)/osrs/palette.c

OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.c=.o)))

-include $(OBJS:.o=.d)

.PHONY: all clean run check

all: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

define make-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1)
	$(CC) $(CFLAGS) -c $(1) -o $(OBJ_DIR)/$(notdir $(1:.c=.o)) -MF $(OBJ_DIR)/$(notdir $(1:.c=.d))
endef

$(foreach src,$(SRCS),$(eval $(call make-goal,$(src))))

$(OBJS): | $(OBJ_DIR)

THIRD_PARTY_CFLAGS := $(filter-out -Wall -Wextra,$(CFLAGS)) -w

$(OBJ_DIR)/bzip.o: $(SRC_DIR)/3rd/bzip/bzip.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -w -c $< -o $@

$(OBJ_DIR)/miniz.o: $(SRC_DIR)/3rd/miniz/miniz.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -w -c $< -o $@

run: $(TARGET)
	./$(TARGET)

check: run

clean:
	rm -rf $(OBJ_DIR) $(TARGET)
