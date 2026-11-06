# The editor's hotkeys (docs/MAPED_HOTKEYS.md): the table, its chords, the
# editor_hotkeys.ini codec and the resolver (editor/editor_hotkeys.c), the IO
# queue tasks for the file, the Hotkeys panel, and the App glue that reads
# the keys and dispatches (app/app_editor_hotkeys.c).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR and CFLAGS exist: rule lines here name no such variable, recipes
# use them freely.
MAPED_CLIENT_SRCS += editor/editor_hotkeys.c editor/task_editor_hotkeys_io.c \
	editor/panels/edpanel_hotkeys.c editor/panels/edpanel_palette_cmd.c app/app_editor_hotkeys.c

# The pure half on its own: every default chord parses and formats back, the
# defaults are conflict-free per context, the ini roundtrip (unbound and
# unknown lines included), the resolver's cases (Ctrl+Z, bare Z, 3, Ctrl+3,
# Delete by context, Shift+[, held camera, repeat timing, a clash), capture,
# and every action's id and row in docs/MAPED_HOTKEYS.md.
.PHONY: test-editor-hotkeys
test-editor-hotkeys:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_hotkeys_test $(TORIMEM_SRCS) editor/editor_hotkeys.c \
		input/torirs_keymap.c editor/test/editor_hotkeys_test.c $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/editor_hotkeys_test ../docs/MAPED_HOTKEYS.md

test-editor: test-editor-hotkeys

# Chords at the action (workbench 6.0, design section 15 surfaces 2 and 3):
# every panel button whose action has a hotkey wears its keycap and Build
# draws it, the tool rail's badges, the Terrain sliders' "Radius  [ ]", a
# rebind relabelling all of them on the next frame, and the chrome's
# truncation ring drained into the console once per record (R2: a 300-byte
# label logs once). Content-free, like test-editor-groups: links the
# client's objects minus main.o.
.PHONY: test-editor-chords
test-editor-chords:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_chords_test editor/test/editor_chords_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS) $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/editor_chords_test

test-editor: test-editor-chords

# Workbench 6.8 (design section 3 "Workspaces" and "Command palette", section
# 15): the four workspaces (defaults, a drag kept per workspace, the layout
# file's [workspace] sections byte-exact, a restart, Reset layout per
# workspace, an old file), the command palette's model over a synthetic table
# (the groups, the 100-row footer, Up / Down / Tab) and the module (Ctrl+K,
# its keys, rows that run), and the Hotkeys panel (its list, the sort, the
# filters, the two-slot capture, the clash marks, Reset all). Content-free,
# like test-editor-groups.
.PHONY: test-editor-workspaces
test-editor-workspaces:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_workspaces_test editor/test/editor_workspaces_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS) $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/editor_workspaces_test

test-editor: test-editor-workspaces
