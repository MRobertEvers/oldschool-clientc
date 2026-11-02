# Catalog palettes (docs/MAPED_STATUS.md "Catalog palette"): the PALETTES
# document's codec (editor/editor_palette.c), its commands through
# editor_cmd.c, the Catalog palette panel and its thumbnails.
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS and the content paths are defined: rule lines here name no
# such variable (it would expand empty), recipes use them freely.

# editor_cmd.c applies PALETTE_* through the codec, so every binary that links
# editor_cmd.c links editor_palette.c -- by the same routes areas.mk gives
# editor_areas.c: the client and torirsmaped through these lists...
MAPED_CLIENT_SRCS += editor/editor_palette.c
MAPED_SERVER_SRCS += editor/editor_palette.c
# The Catalog palette panel and the App's thumbnails of its slots.
MAPED_CLIENT_SRCS += editor/panels/edpanel_catalog_palette.c app/app_catalog_palette.c

# ...and the tests that list their sources by hand (areas.mk's list).
test-editor-cmd test-editor-save test-torirsmaped-embed test-editor-cfg test-editor-alloc \
test-editor-vars test-editor-if torirsmapedctl: \
	TORIMEM_SRCS += editor/editor_palette.c

# The codec on its own: parse -> emit byte-identical (comments, blank lines,
# CRLF, a missing final newline), the grammar's refusals with their line,
# format -> parse, the first-free-slot rule, every command shape forward and
# back byte for byte (the header an empty file gains and loses, a remove's
# undo at its old index), the plans (add, rename moving every line, delete,
# move and swap) applied and undone, a byte mismatch refused, the History
# labels, and the v2 wire form of both command kinds.
.PHONY: test-editor-palette
test-editor-palette:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_palette_test $(TORIMEM_SRCS) editor/editor_palette.c \
		torirsmaped/torirs_maped_cmdwire.c torirsmaped/torirs_maped_buf.c \
		editor/test/editor_palette_test.c
	./$(OBJ_DIR)/editor_palette_test

test-editor: test-editor-palette

# The panel, headless, against the real chrome model and an embedded
# ToriRSMapEd over a fixture the test writes under $(OBJ_DIR)/tmp/editor_panels_palette
# (a catalog of two locs, an npc, an obj, and editor/palettes.txt): the list,
# a slot click picking and arming Place with the slot's pose, Add current
# pick filling slot 1 then 2, a second window seeing the slots after a pump
# and picking for the main one, the slot menu (swap, pose, remove), Rename /
# Delete as one undo step each, the thumbnail cache, Save. Links the client's
# objects minus main.o, as test-editor-panels does.
.PHONY: test-editor-panels-palette
test-editor-panels-palette:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_panels_palette_test editor/test/editor_panels_palette_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/editor_panels_palette
	mkdir -p $(OBJ_DIR)/tmp
	./$(OBJ_DIR)/editor_panels_palette_test $(OBJ_DIR)/tmp/editor_panels_palette

test-editor: test-editor-panels-palette
