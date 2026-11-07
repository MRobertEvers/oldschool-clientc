# Named areas and square collections (docs/MAPED_STATUS.md "Areas and
# Collections"): the AREAS document's codec (editor/editor_areas.c), its
# commands through editor_cmd.c, the Areas panel and the world overlay.
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS and the content paths are defined: rule lines here name no
# such variable (it would expand empty), recipes use them freely.

# editor_cmd.c applies AREA_* / COLLECTION_* through the codec, so every
# binary that links editor_cmd.c links editor_areas.c: the client and
# torirsmaped through these lists...
MAPED_CLIENT_SRCS += editor/editor_areas.c editor/panels/edpanel_areas.c
MAPED_SERVER_SRCS += editor/editor_areas.c

# ...and the tests that list their sources by hand through a target-specific
# append (iface-cs2.mk's pattern; each recipe names $(TORIMEM_SRCS)). The
# controller formats and parses rows for its area verbs. test-catalog and
# test-builder link $(MAPED_SERVER_SRCS), which already carries it.
test-editor-cmd test-editor-save test-torirsmaped-embed test-editor-cfg test-editor-alloc \
test-editor-vars test-editor-if torirsmapedctl: \
	TORIMEM_SRCS += editor/editor_areas.c

# The codec on its own: parse -> emit byte-identical (comments, blank lines,
# CRLF, a missing final newline), the grammar's refusals with their line,
# format -> parse, every command shape forward and back byte for byte (the
# header an empty file gains and loses, a remove's undo at its old index, a
# hand-formatted row restored exactly, a rename, a taken name, a stale
# command), the History labels, and the v2 wire form of each command kind.
.PHONY: test-editor-areas
test-editor-areas:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_areas_test $(TORIMEM_SRCS) editor/editor_areas.c \
		torirsmaped/torirs_maped_cmdwire.c torirsmaped/torirs_maped_buf.c \
		editor/test/editor_areas_test.c $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/editor_areas_test

test-editor: test-editor-areas
