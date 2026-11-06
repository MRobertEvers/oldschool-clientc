# Workbench step 6.5, part 2 (docs/MAPED_STATUS.md "Workbench step 6.5, part
# 2"; design section 13, "Instance helper"): the planner
# (editor/editor_instance.c: the free rectangle in instance space, rebasing,
# the copy's commands, the trigger script), the world-map area codec
# (editor/editor_wma.c, worldmap/areas/<name>.wma, TEXT key "wma:<name>") and
# the dialog the picker's Instance... opens (editor/panels/edpanel_instance.c).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS, OBJS and the content paths are defined: rule lines here name
# no such variable (it would expand empty), recipes use them freely.

# The two documents are TEXT documents written with TEXT_SPLICE commands
# (editor_cmd.c never calls either codec), so only the client links them.
MAPED_CLIENT_SRCS += editor/editor_instance.c editor/editor_wma.c editor/panels/edpanel_instance.c

# Pure, content-free: the free-rectangle search over synthetic atlases (the
# lowest fit, a taken square in the way, holes, nothing fits, the free
# count), rebasing (keys, tiles, spawn rows, a level range, suppressions),
# coords both ways, the copy's commands for each "Copy" choice, the trigger
# text in both entries, and the .wma codec: parse -> emit byte-identical
# (comments, CRLF, no final newline), its refusals with their line, and a new
# area's text parsed back to the squares it claims.
.PHONY: test-editor-instance
test-editor-instance:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_instance_test $(TORIMEM_SRCS) editor/editor_instance.c \
		editor/editor_wma.c editor/editor_spawn.c editor/test/editor_instance_test.c
	./$(OBJ_DIR)/editor_instance_test

test-editor: test-editor-instance

# The dialog, headless, against the real chrome model and an embedded
# ToriRSMapEd over a SYNTHETIC content tree the test writes under
# $(OBJ_DIR)/tmp/editor_instance (two hand-written squares with locs and
# edited spawns): the picker's Instance... opens it with the selection as
# its source; Find free marks the first free rectangle in the picker; the
# refusals (a bad name, a taken key with its owner, a source too wide for the
# runtime chip, the runtime chip disabled with its reason); Create as ONE
# undo step on the authority (two DOC_CREATEs, the spawn batch rebased, the
# .wma and the .rs2), nothing on disk until Save, then the files; the .wma
# read back by the server catalog's atlas reader (the squares claimed by the
# new area); Undo back to empty documents. Links the client's objects minus
# main.o, as test-editor-stamps-ops does; no cache, no OSRS-Content.
.PHONY: test-editor-instance-ops
test-editor-instance-ops:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_instance_ops_test editor/test/editor_instance_ops_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/editor_instance
	mkdir -p $(OBJ_DIR)/tmp
	./$(OBJ_DIR)/editor_instance_ops_test $(OBJ_DIR)/tmp/editor_instance

test-editor: test-editor-instance-ops
