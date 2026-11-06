# Workbench step 6.0b, the editor's half (design section 2 "R1 in full",
# section 3 "Every panel pops out"): the chrome's draw list at the integer
# scales against digests recorded before the step, the chrome scale snap
# table, View > Chrome scale and View > Text size, the device-local
# editor_prefs.ini, the Dock button and TORIRSMAPED_STATE_DOCK_PANEL across
# two sessions of one embedded ToriRSMapEd, and the modules a panel-only
# window loads no world for. Content-free: links the client's objects minus
# main.o, as test-editor-groups does; the embedded server's content dir is a
# scratch directory under $(OBJ_DIR)/tmp.
#
# Included BEFORE OBJ_DIR and CFLAGS exist (see tools-panels.mk): rule lines
# here name no such variable, recipes use them freely.

# The device-local view preferences (editor_prefs.ini): the pure codec and
# its two IO queue tasks, linked into every client build like the layout
# file's (editor-layout.mk).
MAPED_CLIENT_SRCS += editor/editor_prefs.c editor/task_editor_prefs_io.c

.PHONY: test-editor-workbench
test-editor-workbench:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_workbench_test editor/test/editor_workbench_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS) $(MAPED_TEST_LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/workbench
	mkdir -p $(OBJ_DIR)/tmp/workbench
	./$(OBJ_DIR)/editor_workbench_test $(abspath $(OBJ_DIR)/tmp/workbench)

test-editor: test-editor-workbench
