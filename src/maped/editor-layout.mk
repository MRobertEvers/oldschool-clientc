# The editor's dock layout persisted as a device-local preference
# (editor_layout.ini, TORIRS_EDITOR_LAYOUT): the pure codec and the two IO
# queue tasks. Linked into every client build like the panels
# (tools-panels.mk); tested by test-editor-panels and, for the panel groups'
# [group] sections, test-editor-groups.
#
# Included BEFORE OBJ_DIR and CFLAGS exist (see tools-panels.mk): only
# variable appends here.
MAPED_CLIENT_SRCS += editor/editor_layout.c editor/task_editor_layout_io.c

# The panel groups (workbench 6.0): the default groups and their tab strips,
# the layout's one place per group, Close / Pop out / maximise on a group's
# title bar, View > Reset groups, and the layout file's [group] sections
# (round trip, and a file from before groups). Content-free: every module
# registered with no session and stand-in App services, so it runs where the
# content tree is not checked out. Links the client's objects minus main.o,
# as test-editor-panels does.
.PHONY: test-editor-groups
test-editor-groups:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_groups_test editor/test/editor_groups_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	./$(OBJ_DIR)/editor_groups_test

test-editor: test-editor-groups
