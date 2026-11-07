# The path painter (the Windows run, 2026-11-06): editor/editor_path.c plans
# a dragged path's tiles and shapes (dirt and pavement, edges fitted to the
# client's overlay shapes and to each other); the Path tool (H) lays it
# (editor_panel.c Editor_PanelPath*, edpanel_ops.c Editor_PathCommands, the
# Terrain panel's Path section, the overlay's preview). editor_path.c rides in
# TOOLS_PANELS_SRCS (maped/tools-panels.mk), so every link that has the ops
# has it.
#
# Included from src/makefile BEFORE OBJ_DIR and CFLAGS are defined: rule lines
# here name no such variable, recipes use them freely.

# The planner on its own, content-free: the shapes' coverage is the client's,
# every shape fits itself, pavement's straight, half-width and diagonal roads
# and a mitred bend, a dirt track continuous with edge shapes on a share of
# it, seeded, and a second stroke joining the first. With PATH_PLANS=<file>
# the plans are written out for a look.
.PHONY: test-editor-path
test-editor-path:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_path_test $(TORIMEM_SRCS) editor/editor_path.c \
		editor/test/editor_path_test.c -lm $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/editor_path_test $(PATH_PLANS)

test-editor: test-editor-path
