# The editor's hotkeys (docs/MAPED_HOTKEYS.md): the table, its chords, the
# editor_hotkeys.ini codec and the resolver (editor/editor_hotkeys.c), the IO
# queue tasks for the file, the Hotkeys panel, and the App glue that reads
# the keys and dispatches (app/app_editor_hotkeys.c).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR and CFLAGS exist: rule lines here name no such variable, recipes
# use them freely.
MAPED_CLIENT_SRCS += editor/editor_hotkeys.c editor/task_editor_hotkeys_io.c \
	editor/panels/edpanel_hotkeys.c app/app_editor_hotkeys.c

# The pure half on its own: every default chord parses and formats back, the
# defaults are conflict-free per context, the ini roundtrip (unbound and
# unknown lines included), the resolver's cases (Ctrl+Z, bare Z, 3, Ctrl+3,
# Delete by context, Shift+[, held camera, repeat timing, a clash), capture,
# and every action's id and row in docs/MAPED_HOTKEYS.md.
.PHONY: test-editor-hotkeys
test-editor-hotkeys:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_hotkeys_test $(TORIMEM_SRCS) editor/editor_hotkeys.c \
		input/torirs_keymap.c editor/test/editor_hotkeys_test.c
	./$(OBJ_DIR)/editor_hotkeys_test ../docs/MAPED_HOTKEYS.md

test-editor: test-editor-hotkeys
