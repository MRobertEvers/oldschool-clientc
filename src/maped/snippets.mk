# Scripts are written in the user's own editor (owner, 2026-11-09): the map
# editor hands over the file, its path, the record's name and RuneScript
# snippets to paste. editor/editor_snippets.c is the snippet catalogue (pure:
# no chrome, no session); the Triggers panel (editor/panels/edpanel_triggers.c)
# lists it, previews one and copies it to the OS clipboard
# (platform/platform_clipboard.h, maped/clipboard.mk). "Show in Explorer /
# Finder" is Editor_HostLocalRevealInFileManager in editor/editor_host_local.c,
# its command line in editor/editor_open_cmd.h.
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS and the content paths are defined: rule lines here name no
# such variable (it would expand empty), recipes use them freely.
MAPED_CLIENT_SRCS += editor/editor_snippets.c

# The catalogue on its own, content-free (no content tree, no sscompile, no
# chrome): every snippet of every kind renders for a short name and for the
# longest a record can have; names the record; starts with the `//` line that
# says which kind of file it goes in; leaves no template mark behind; balances
# its braces, parentheses, brackets and string quotes; keeps every line inside
# a preview row; ends with a line break; and the titles fit a dropdown row and
# do not repeat. -UNDEBUG: the renderer asserts a buffer that cannot hold the
# text, and that assert is part of what the test runs.
.PHONY: test-editor-snippets
test-editor-snippets:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -UNDEBUG -o $(OBJ_DIR)/editor_snippets_test editor/editor_snippets.c \
		editor/test/editor_snippets_test.c $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/editor_snippets_test

test-editor: test-editor-snippets
