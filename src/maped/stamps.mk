# Workbench 6.3, part 2 (docs/MAPED_STATUS.md "Workbench step 6.3, part 2"):
# the STAMPS documents' codec (editor/editor_stamps.c), the stamp, level
# move and Find same ops and their actions (editor/panels/edpanel_ops_stamps.c),
# and the Stamps panel the Catalog opens (editor/panels/edpanel_stamps.c).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS, OBJS and the content paths are defined: rule lines here name
# no such variable (it would expand empty), recipes use them freely.

# A stamp is a TEXT document (key "stamp:<name>", editor/stamps/<name>.stamp;
# the list "stamps:index"), written with TEXT_SPLICE commands: editor_cmd.c
# never calls the codec, so only the client links it.
MAPED_CLIENT_SRCS += editor/editor_stamps.c editor/panels/edpanel_ops_stamps.c editor/panels/edpanel_stamps.c

# The codec on its own, content-free: parse -> emit byte-identical (comments,
# blank lines, CRLF, a missing final newline), the grammar's refusals with
# their line, format -> parse for every piece kind, a new stamp's text, the
# quarter turn as a unit (offsets, multi-tile footprints, rotations, overlay
# rotations; four turns are none), the splice plan (head and tail kept,
# chunks within a command, applied in order it rebuilds the new text, and
# backwards the old) and the index gaining a line once.
.PHONY: test-editor-stamps
test-editor-stamps:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_stamps_test $(TORIMEM_SRCS) editor/editor_stamps.c \
		editor/test/editor_stamps_test.c
	./$(OBJ_DIR)/editor_stamps_test

test-editor: test-editor-stamps

# The ops and actions, headless, against the real chrome model and an
# embedded ToriRSMapEd over a SYNTHETIC content tree the test writes under
# $(OBJ_DIR)/tmp/editor_stamps (one hand-written square, m50_50: a few tiles,
# a wall run, a 2x1 table, an upper floor; an edited .spawn): a stamp saved
# from a selection (its file and the index on disk, one undo step), armed,
# turned and dropped as ONE undo step whose undo restores the square's .jl2
# and .jm2 byte for byte, the turned text round-tripping; the marquee's
# Ctrl+C / Ctrl+V carrying its locs after tile commands identical to the
# tile paste's; Ctrl+PgUp / PgDn moving the selection a level and refusing
# (level 4, a taken key) with a status sentence; Find same; the gizmo's
# Alt-copy and ring turn through their actions; the Locs panel's new rows,
# Follow and the clickable strip; the Catalog's Stamps chip. Links the
# client's objects minus main.o, as test-editor-panels does; no cache, no
# OSRS-Content.
.PHONY: test-editor-stamps-ops
test-editor-stamps-ops:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_stamps_ops_test editor/test/editor_stamps_ops_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/editor_stamps
	mkdir -p $(OBJ_DIR)/tmp
	./$(OBJ_DIR)/editor_stamps_ops_test $(OBJ_DIR)/tmp/editor_stamps

test-editor: test-editor-stamps-ops
