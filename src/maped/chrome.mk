# Phase 2, owner `chrome` (docs/MAPED_IMPLEMENTATION.md section 1).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS and the content paths are defined: rule lines here name no
# such variable (it would expand empty), recipes use them freely.

# ---- the baked skin ---------------------------------------------------------
#
# Two targets, because one half needs a cache and the other must not.
#
# chrome-skin (no cache): the bake is reproducible rather than a comment.
# Re-derives the 2x and 3x skin tiers (tools/chrome_skin_tiers.py: GLYPH slots
# through the mask route, TEXTURED through xBR, from the 1x bake and
# engine/torirs_chrome_skin_class.h) and the font tiers' derivation and
# hand-tuning queue (tools/chrome_font_tiers.py), and FAILS when either differs
# from what is committed; then runs the tier test. A clean tree passes. To
# refresh after changing the 1x bake, the class table or a tool: run both
# tools without --check and commit what they write (docs/CHROME_SCALE.md).
# Review sheets land in $(OBJ_DIR)/tmp/chrome_skin/{tiers,font_tiers}.png.
#
# chrome-skin-rebake (needs the cache): re-runs the recipe recorded in the
# baked file's own header -- the list of `--sprite` / `--stamp` arguments under
# "Regenerate with:" -- against the pinned cache, regenerates the web skin PNGs
# from the result, and then rewrites the tiers from the new 1x bake. The header
# is the single source of truth: to add a slot, run spritebake once by hand
# with the longer list (as docs/MAPED_DESIGN.html #chrome describes), add its
# row to engine/torirs_chrome_skin_class.h, and from then on this target
# reproduces it.
#
# spritebake is built privately under $(OBJ_DIR)/tmp/chrome_skin: the binary
# checked in beside its source predates the osrs241 profile, and the tools
# makefile would write over it. The cache is READ ONLY here.
#
# Visual check: `make -C src chrome-skin-rebake` writes the contact sheet to
# $(OBJ_DIR)/tmp/chrome_skin/sheet.bmp -- inspect a crop of it.
CHROME_SKIN_REV ?= osrs242
# Relative to the repository root, which is where the bake runs from: the path
# is written into the generated header, and it has to be the same on every
# machine that re-runs it.
CHROME_SKIN_CACHE ?= cache.osrs242
CHROME_SKIN_BAKE = engine/torirs_chrome_skin_baked.c
CHROME_SKIN_RSCACHE = $(REPO_ROOT)/3rd/rscache
CHROME_SKIN_TOOLS = $(CHROME_SKIN_RSCACHE)/tools

.PHONY: chrome-skin chrome-skin-rebake test-chrome-skin-tiers chrome-font-vector
chrome-skin:
	mkdir -p $(OBJ_DIR)/tmp/chrome_skin
	python3 $(REPO_ROOT)/tools/chrome_skin_tiers.py --check \
		--sheet $(OBJ_DIR)/tmp/chrome_skin/tiers.png
	python3 $(REPO_ROOT)/tools/chrome_font_tiers.py --check \
		--sheet $(OBJ_DIR)/tmp/chrome_skin/font_tiers.png
	$(MAKE) chrome-font-vector
	$(MAKE) test-chrome-skin-tiers

# ---- the vector faces --------------------------------------------------------
#
# chrome-font-vector (no cache): the synthesized vector faces are reproducible,
# not a comment, the same way. tools/chrome_font_vector.py re-synthesizes
# Small, Body and Menu from the 1x masks in engine/torirs_debug_font_baked.c
# (depixelize: Kopf and Lischinski 2011 adapted to 1-bit masks) and FAILS when
# any of what it writes differs from what is committed: the TTFs under
# engine/fonts/, the web faces res/plugin_chrome/font/ToriRS*-Vector.{ttf,woff,
# eot}, and engine/torirs_chrome_font_vector_baked.{c,h} (the TTF bytes the
# native rasteriser reads), or when a face fails its own checks (advances equal
# to the bitmap's x 64, metrics, no source pixel lost). Needs Python 3 with
# numpy, Pillow and fontTools 4.59.2. To refresh: run the tool without --check
# and commit what it writes. Review sheets and report.json land in
# $(OBJ_DIR)/tmp/chrome_font_vector/.
chrome-font-vector:
	mkdir -p $(OBJ_DIR)/tmp/chrome_font_vector
	python3 $(REPO_ROOT)/tools/chrome_font_vector.py --check \
		--sheets $(OBJ_DIR)/tmp/chrome_font_vector

# Every slot at 1x, 2x and 3x of the right size and kind; the font tiers
# through their one accessor. Asserts on (-UNDEBUG).
test-chrome-skin-tiers:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -UNDEBUG -o $(OBJ_DIR)/chrome_skin_tiers_test \
		ui/test/chrome_skin_tiers_test.c engine/torirs_chrome_skin_baked.c \
		engine/torirs_chrome_skin_tiers.c engine/torirs_debug_font_baked.c $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/chrome_skin_tiers_test

chrome-skin-rebake:
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
		| grep -oE -- '--(sprite|stamp|disc) [^ ]+' > $(OBJ_DIR)/tmp/chrome_skin/recipe.txt
	test -s $(OBJ_DIR)/tmp/chrome_skin/recipe.txt
	cd $(REPO_ROOT) && $(SRC_DIR)/$(OBJ_DIR)/tmp/chrome_skin/spritebake \
		--rev $(CHROME_SKIN_REV) $(CHROME_SKIN_CACHE) \
		$$(cat $(SRC_DIR)/$(OBJ_DIR)/tmp/chrome_skin/recipe.txt) \
		--out src/$(CHROME_SKIN_BAKE) --header src/engine/torirs_chrome_skin_baked.h \
		--sheet $(SRC_DIR)/$(OBJ_DIR)/tmp/chrome_skin/sheet.bmp
	python3 $(REPO_ROOT)/tools/spritebake_png.py $(CHROME_SKIN_BAKE) \
		$(REPO_ROOT)/res/plugin_chrome/skin
	python3 $(REPO_ROOT)/tools/chrome_skin_tiers.py

# ---- one sync test per editor widget kind ------------------------------------
#
# TREE, VLIST, ICONBUTTON/TOOLBAR, SLIDER, RADIO, MODAL, TOOLTIP, CODE, GRAPH,
# SPLITTER/docking, the baked slots, the raised capacities, BUTTON (its chord
# keycap), the truncation ring, the rational chrome scale (its SCALE command,
# equal rows, and the photographs $(OBJ_DIR)/scale_q<4..12>.bmp) and the bake
# at every scale (glyph atlases, stems, skin tiers): each drives
# the real model and the real executor seam (ToriRSChromeSync over a recorder).
# Asserts on (-UNDEBUG), like the other chrome seam tests.
CHROME_WIDGET_TESTS = main tree vlist iconbutton slider radio modal tooltip code graph \
	splitter skin capacity button truncation scale glyphs gesture

.PHONY: test-chrome-widgets
test-chrome-widgets:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -UNDEBUG -UTORIRS_CHROME_EXEC_WEB_AVAILABLE \
		-UTORIRS_CHROME_EXEC_BROWSER_AVAILABLE -UTORIRS_CHROME_SURFACE_SDL \
		-DCW_OUT_DIR='"$(OBJ_DIR)"' \
		-o $(OBJ_DIR)/chrome_widget_sync_test $(TORIMEM_SRCS) \
		$(addprefix ui/test/chrome_widget_sync_,$(addsuffix .c,$(CHROME_WIDGET_TESTS))) \
		ui/uitree_debug_overlay.c ui/torirs_chrome_exec.c ui/torirs_chrome_exec_kind.c \
		ui/torirs_chrome_rail.c ui/torirs_chrome_bake.c engine/torirs_chrome_skin_baked.c \
		engine/torirs_chrome_skin_tiers.c engine/torirs_debug_font_baked.c \
		engine/torirs_chrome_font_vector_baked.c -lm $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/chrome_widget_sync_test
	node ui/test/chrome_widgets_dom_test.js

# Runs with the rest of the editor's tests.
test-editor: test-chrome-widgets test-chrome-skin-tiers
