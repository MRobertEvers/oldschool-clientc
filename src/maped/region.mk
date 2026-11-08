# Region painting (docs/MAPED_DESIGN.html #terrain, 2026-11-07): overlays and
# underlays whose border does not follow the grid. editor/editor_region.c is
# the coverage grid, the fit to the client's overlay shapes and the settle
# pass the path painter also plans through; the Region brush (N), the Lasso
# (J) and Tidy (Y) lay it (editor/panels/edpanel_region.c, the Terrain
# panel's Region section, the overlay's preview). editor_region.c rides in
# TOOLS_PANELS_SRCS (maped/tools-panels.mk), so every link that has the ops
# has it.
#
# Included from src/makefile BEFORE OBJ_DIR and CFLAGS are defined: rule lines
# here name no such variable, recipes use them freely.

# The planner on its own, content-free: every shape fits itself, a laid region
# replanned at smooth 0 is what was laid, a disc's rim, add then subtract,
# a second stroke joining the first, another overlay next door, the lasso,
# Tidy, hardness, the underlay dither, the run a tile belongs to. Built with
# asserts on. With REGION_PLANS=<file> the plans are written out for a look.
.PHONY: test-editor-region
test-editor-region:
	mkdir -p $(OBJ_DIR)
	$(CC) $(filter-out -DNDEBUG,$(CFLAGS)) -UNDEBUG -o $(OBJ_DIR)/editor_region_test $(TORIMEM_SRCS) \
		editor/editor_region.c editor/test/editor_region_test.c -lm $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/editor_region_test $(REGION_PLANS)

test-editor: test-editor-region

# The commands against a document (editor/panels/edpanel_region.c): an
# in-memory document of two empty squares, no content, no authority. A disc
# across the squares' border in both, laid twice is laid once, taken off
# again, a square not open, another overlay next door, the underlay's
# dither, Tidy's run (a second run beside it untouched), the room running out.
.PHONY: test-editor-region-ops
test-editor-region-ops:
	mkdir -p $(OBJ_DIR)
	$(CC) $(filter-out -DNDEBUG,$(CFLAGS)) -UNDEBUG -o $(OBJ_DIR)/editor_region_ops_test $(TORIMEM_SRCS) \
		editor/editor_region.c editor/panels/edpanel_region.c editor/editor_doc.c editor/editor_cfg.c \
		editor/editor_if.c editor/editor_spawn.c editor/test/editor_region_ops_test.c -lm $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/editor_region_ops_test

test-editor: test-editor-region-ops

# The tools in the client: the stroke, the lasso, Tidy and the preview
# (editor/editor_panel_region.c, beside editor_panel.c) and their commands
# (editor/panels/edpanel_region.c, beside edpanel_ops.c).
MAPED_CLIENT_SRCS += editor/editor_panel_region.c editor/panels/edpanel_region.c
