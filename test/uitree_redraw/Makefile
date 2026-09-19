# Standalone deterministic UITree redraw parity/performance harness.
#
# The harness source always comes from this candidate worktree. SOURCE_ROOT
# selects the implementation under test, so the same driver can be linked once
# against origin/v3 and once against the candidate without copying fixtures.

SOURCE_ROOT ?= ../..
BUILD_DIR ?= $(CURDIR)/build
CANDIDATE ?= 1
CC ?= cc

SOURCE_ROOT := $(abspath $(SOURCE_ROOT))
BUILD_DIR := $(abspath $(BUILD_DIR))
TARGET := $(BUILD_DIR)/uitree_redraw_harness
CHROME_TARGET := $(BUILD_DIR)/uitree_debug_overlay_visual
CHROME_OUT_DIR ?= $(BUILD_DIR)/chrome_visual

SRC := \
	$(SOURCE_ROOT)/src/ui/uitree.c \
	$(SOURCE_ROOT)/src/ui/uitree_hook.c \
	$(SOURCE_ROOT)/src/ui/uitree_component_options.c \
	$(SOURCE_ROOT)/src/engine/torirs_component_hook.c \
	$(SOURCE_ROOT)/src/ui/uitree_layout.c \
	$(SOURCE_ROOT)/src/ui/uitree_build.c \
	$(SOURCE_ROOT)/src/ui/uitree_host.c \
	$(SOURCE_ROOT)/src/ui/uitree_frame.c \
	$(SOURCE_ROOT)/src/ui/uitree_role.c \
	$(SOURCE_ROOT)/src/ui/uitree_input.c \
	$(SOURCE_ROOT)/src/ui/uitree_hover.c \
	$(SOURCE_ROOT)/src/ui/uitree_scroll.c \
	$(SOURCE_ROOT)/src/ui/uitree_emit.c \
	$(SOURCE_ROOT)/src/ui/uitree_inv_view.c \
	$(SOURCE_ROOT)/src/ui/uitree_obj_cell.c \
	$(SOURCE_ROOT)/src/ui/uitree_cross.c \
	$(SOURCE_ROOT)/src/ui/uitree_minimenu.c \
	$(SOURCE_ROOT)/src/ui/uitree_hovertext.c \
	$(SOURCE_ROOT)/src/ui/uitree_debug_overlay.c \
	$(SOURCE_ROOT)/src/ui/uitree_interact.c \
	$(SOURCE_ROOT)/src/input/torirs_input.c \
	$(SOURCE_ROOT)/src/input/torirs_keymap.c \
	$(SOURCE_ROOT)/src/perf/torirs_perf.c \
	$(SOURCE_ROOT)/src/engine/uitree_cmd_render.c \
	$(SOURCE_ROOT)/src/engine/torirs_debug_font_baked.c \
	$(SOURCE_ROOT)/src/render/torirs_frame.c \
	$(SOURCE_ROOT)/src/render/torirs_pick.c \
	$(SOURCE_ROOT)/src/render/torirs_polygon.c \
	$(SOURCE_ROOT)/src/render/torirs_arc.c \
	$(SOURCE_ROOT)/src/platform/platform_sdl2_renderer_soft3d.c \
	$(SOURCE_ROOT)/3rd/bmp/bmp.c \
	$(SOURCE_ROOT)/3rd/toridraw/toridraw_unity.c \
	$(SOURCE_ROOT)/3rd/toridraw/toridraw_font.c \
	$(SOURCE_ROOT)/3rd/toridraw/toridraw_model_sprite.c

# Isolated version of src's debug-overlay raster test.  origin/v3's source
# Makefile omits torirs_perf.c from this link, while the real frame/Soft3D
# implementation now calls its symbols.  Keep the repaired lane here so both
# checkouts are built by the same candidate-owned test driver.
CHROME_SRC := \
	$(SOURCE_ROOT)/src/ui/test/uitree_debug_overlay_visual.c \
	$(SOURCE_ROOT)/src/ui/uitree_debug_overlay.c \
	$(SOURCE_ROOT)/src/engine/torirs_debug_font_baked.c \
	$(SOURCE_ROOT)/src/engine/torirs_chrome_skin_baked.c \
	$(SOURCE_ROOT)/src/perf/torirs_perf.c \
	$(SOURCE_ROOT)/src/render/torirs_frame.c \
	$(SOURCE_ROOT)/src/render/torirs_pick.c \
	$(SOURCE_ROOT)/src/render/torirs_polygon.c \
	$(SOURCE_ROOT)/src/render/torirs_arc.c \
	$(SOURCE_ROOT)/src/platform/platform_sdl2_renderer_soft3d.c \
	$(SOURCE_ROOT)/3rd/bmp/bmp.c \
	$(SOURCE_ROOT)/3rd/toridraw/toridraw_unity.c \
	$(SOURCE_ROOT)/3rd/toridraw/toridraw_font.c \
	$(SOURCE_ROOT)/3rd/toridraw/toridraw_model_sprite.c

CPPFLAGS += \
	-DCANDIDATE=$(CANDIDATE) \
	-D_POSIX_C_SOURCE=200809L \
	-I$(SOURCE_ROOT)/src \
	-I$(SOURCE_ROOT)/src/ui \
	-I$(SOURCE_ROOT) \
	-I$(SOURCE_ROOT)/3rd/toridraw \
	-I$(SOURCE_ROOT)/3rd/bmp \
	-I$(SOURCE_ROOT)/3rd/rscache/include \
	-I$(SOURCE_ROOT)/3rd/rscache/src \
	-I$(SOURCE_ROOT)/3rd/trspk \
	-I$(SOURCE_ROOT)/3rd/hmap \
	-I$(SOURCE_ROOT)/3rd/rsareabuf \
	-I$(SOURCE_ROOT)/3rd/rsprot/src \
	-I$(SOURCE_ROOT)/3rd/rsprot/include \
	-I$(SOURCE_ROOT)/3rd/rsprot

# _POSIX_C_SOURCE gives Linux strdup/clock_gettime declarations.  Darwin also
# needs its extension namespace for the Soft3D clear's memset_pattern4.
ifeq ($(shell uname -s),Darwin)
CPPFLAGS += -D_DARWIN_C_SOURCE
endif

CFLAGS ?= -std=c11 -O3 -g -fno-omit-frame-pointer
CFLAGS += -Wall -Wextra -Werror=incompatible-pointer-types
LDLIBS += -lm

.PHONY: all chrome-visual clean FORCE
all: $(TARGET)

# SOURCE_ROOT/CANDIDATE are command-line build identities rather than files.
# Re-link on every explicit invocation so reusing BUILD_DIR cannot silently
# execute a binary compiled against the other checkout.
FORCE:

$(TARGET): FORCE $(CURDIR)/uitree_redraw_harness.c $(SRC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CURDIR)/uitree_redraw_harness.c $(SRC) $(LDFLAGS) $(LDLIBS)

# The upstream visual writes `build/debug_overlay_*.bmp` relative to cwd.
# Running in CHROME_OUT_DIR gives the caller an isolated, predictable artifact
# tree without patching either checkout's source or Makefile.
chrome-visual: $(CHROME_TARGET)
	@mkdir -p "$(CHROME_OUT_DIR)/build"
	cd "$(CHROME_OUT_DIR)" && "$(CHROME_TARGET)"

$(CHROME_TARGET): FORCE $(CHROME_SRC)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CHROME_SRC) $(LDFLAGS) $(LDLIBS)

clean:
	rm -f $(TARGET) $(CHROME_TARGET)
