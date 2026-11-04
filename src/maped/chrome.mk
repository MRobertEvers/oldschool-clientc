# Phase 2, owner `chrome` (docs/MAPED_IMPLEMENTATION.md section 1).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS and the content paths are defined: rule lines here name no
# such variable (it would expand empty), recipes use them freely.

# ---- the baked skin ---------------------------------------------------------
#
# Re-runs the recipe recorded in the baked file's own header -- the list of
# `--sprite` / `--stamp` arguments under "Regenerate with:" -- against the
# pinned cache, then regenerates the web skin PNGs from the result. The header
# is the single source of truth: to add a slot, run spritebake once by hand
# with the longer list (as docs/MAPED_DESIGN.html #chrome describes), and from
# then on this target reproduces it.
#
# spritebake is built privately under $(OBJ_DIR)/tmp/chrome_skin: the binary
# checked in beside its source predates the osrs241 profile, and the tools
# makefile would write over it. The cache is READ ONLY here.
#
# Visual check: `make -C src chrome-skin` writes the contact sheet to
# $(OBJ_DIR)/tmp/chrome_skin/sheet.bmp -- inspect a crop of it.
CHROME_SKIN_REV ?= osrs241
# Relative to the repository root, which is where the bake runs from: the path
# is written into the generated header, and it has to be the same on every
# machine that re-runs it.
CHROME_SKIN_CACHE ?= cache.osrs241
CHROME_SKIN_BAKE = engine/torirs_chrome_skin_baked.c
CHROME_SKIN_RSCACHE = $(REPO_ROOT)/3rd/rscache
CHROME_SKIN_TOOLS = $(CHROME_SKIN_RSCACHE)/tools

.PHONY: chrome-skin
chrome-skin:
	mkdir -p $(OBJ_DIR)/tmp/chrome_skin
	$(CC) -std=c11 -O2 -w -I$(CHROME_SKIN_RSCACHE)/include -I$(CHROME_SKIN_RSCACHE)/src \
		-I$(CHROME_SKIN_RSCACHE) -I$(REPO_ROOT)/3rd/bmp -I$(REPO_ROOT)/3rd/bzip \
		-I$(REPO_ROOT)/3rd/miniz -I$(REPO_ROOT)/3rd/xteas -I$(REPO_ROOT)/3rd/ini \
		-I$(CHROME_SKIN_TOOLS)/common \
		-o $(OBJ_DIR)/tmp/chrome_skin/spritebake \
		$(CHROME_SKIN_TOOLS)/spritebake/main.c \
		$(addprefix $(CHROME_SKIN_TOOLS)/common/,tool_profile.c asset_access.c anim_affinity.c \
			port_plan.c cache_write.c transcode.c cs2_db_columns.c) \
		$(CHROME_SKIN_RSCACHE)/rscache_unity.c $(REPO_ROOT)/3rd/bzip/bzip.c \
		$(REPO_ROOT)/3rd/bzip/bzip_encode.c $(REPO_ROOT)/3rd/miniz/miniz.c \
		$(REPO_ROOT)/3rd/bmp/bmp.c $(REPO_ROOT)/3rd/xteas/xteas.c $(REPO_ROOT)/3rd/ini/ini.c \
		-lm -lpthread
	sed -n '/Regenerate with:/,/--out/p' $(CHROME_SKIN_BAKE) \
		| grep -oE -- '--(sprite|stamp) [^ ]+' > $(OBJ_DIR)/tmp/chrome_skin/recipe.txt
	test -s $(OBJ_DIR)/tmp/chrome_skin/recipe.txt
	cd $(REPO_ROOT) && $(SRC_DIR)/$(OBJ_DIR)/tmp/chrome_skin/spritebake \
		--rev $(CHROME_SKIN_REV) $(CHROME_SKIN_CACHE) \
		$$(cat $(SRC_DIR)/$(OBJ_DIR)/tmp/chrome_skin/recipe.txt) \
		--out src/$(CHROME_SKIN_BAKE) --header src/engine/torirs_chrome_skin_baked.h \
		--sheet $(SRC_DIR)/$(OBJ_DIR)/tmp/chrome_skin/sheet.bmp
	python3 $(REPO_ROOT)/tools/spritebake_png.py $(CHROME_SKIN_BAKE) \
		$(REPO_ROOT)/res/plugin_chrome/skin

# ---- one sync test per editor widget kind ------------------------------------
#
# TREE, VLIST, ICONBUTTON/TOOLBAR, SLIDER, RADIO, MODAL, TOOLTIP, CODE, GRAPH,
# SPLITTER/docking, the baked slots, the raised capacities, BUTTON (its chord
# keycap) and the truncation ring: each drives
# the real model and the real executor seam (ToriRSChromeSync over a recorder).
# Asserts on (-UNDEBUG), like the other chrome seam tests.
CHROME_WIDGET_TESTS = main tree vlist iconbutton slider radio modal tooltip code graph \
	splitter skin capacity button truncation

.PHONY: test-chrome-widgets
test-chrome-widgets:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -UNDEBUG -UTORIRS_CHROME_EXEC_WEB_AVAILABLE \
		-UTORIRS_CHROME_EXEC_BROWSER_AVAILABLE -UTORIRS_CHROME_SURFACE_SDL \
		-o $(OBJ_DIR)/chrome_widget_sync_test $(TORIMEM_SRCS) \
		$(addprefix ui/test/chrome_widget_sync_,$(addsuffix .c,$(CHROME_WIDGET_TESTS))) \
		ui/uitree_debug_overlay.c ui/torirs_chrome_exec.c ui/torirs_chrome_exec_kind.c \
		ui/torirs_chrome_rail.c engine/torirs_chrome_skin_baked.c
	./$(OBJ_DIR)/chrome_widget_sync_test
	node ui/test/chrome_widgets_dom_test.js

# Runs with the rest of the editor's tests.
test-editor: test-chrome-widgets
