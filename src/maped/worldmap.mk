# The square picker's world map (docs/MAPED_STATUS.md row 9b): the CS2 world
# map's own picture of the page's area under the picker's grid.
#
#   game/rs_worldmap_overview.c   one-pixel-per-tile bakes from the world-map
#                                 renderer, resampled into a window of world
#                                 squares (the App's services)
#   editor/editor_worldmap.c      what the picker asks for and when, and the
#                                 picture it draws (dimmed off-page squares,
#                                 the grid)
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR and CFLAGS exist: rule lines here name no such variable, recipes
# use them freely.
MAPED_CLIENT_SRCS += editor/editor_worldmap.c game/rs_worldmap_overview.c

# The pure halves on their own: the region-to-pixels mapping (whole-region
# and chunk records, the box filter, north up, the window edges), the
# picture's composition (dimmed squares, black where the map has nothing, the
# grid), and -- with the picker and the chrome linked, content-free -- the
# level of detail by zoom, the GRAPH item budget, instance space and lanes,
# and the visible-first polling against fake services.
.PHONY: test-editor-worldmap
test-editor-worldmap:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -UNDEBUG -UTORIRS_CHROME_EXEC_WEB_AVAILABLE -UTORIRS_CHROME_EXEC_BROWSER_AVAILABLE \
		-UTORIRS_CHROME_SURFACE_SDL -o $(OBJ_DIR)/editor_worldmap_test $(TORIMEM_SRCS) \
		game/rs_worldmap_overview.c editor/editor_worldmap.c editor/panels/edpanel_picker.c \
		editor/test/editor_worldmap_test.c editor/test/editor_worldmap_test_stubs.c \
		ui/uitree_debug_overlay.c ui/torirs_chrome_exec.c ui/torirs_chrome_exec_kind.c \
		ui/torirs_chrome_rail.c ui/torirs_chrome_bake.c engine/torirs_chrome_skin_baked.c \
		engine/torirs_chrome_skin_tiers.c engine/torirs_debug_font_baked.c engine/torirs_chrome_ui_fonts_baked.c -lm $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/editor_worldmap_test

test-editor: test-editor-worldmap
