# Workbench 6.7, part 1 (docs/MAPED_STATUS.md "Workbench step 6.7, part 1"):
# the encounter builder -- the ENCOUNTERS document's codec
# (editor/editor_encounters.c, with the Drops tab's reader and the Combat
# tab's templates), the npc stats ledger's codec and the derived cells
# (editor/editor_npcstats.c), the panel (editor/panels/edpanel_encounter.c,
# module "encounter") and the controller's verbs
# (torirsmaped/torirs_mapedctl_encounters.c).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS, OBJS and the content paths are defined: rule lines here name
# no such variable (it would expand empty), recipes use them freely.

# Both documents are TEXT documents written with TEXT_SPLICE commands
# (editor_stamps.c plans them): editor_cmd.c never calls either codec, so only
# the client links them...
MAPED_CLIENT_SRCS += editor/editor_encounters.c editor/editor_npcstats.c editor/panels/edpanel_encounter.c

# ...and the controller, whose encounters / encounter-set / encounter-remove
# verbs parse and plan the document (the stamps codec carries the splice
# planner).
torirsmapedctl: \
	TORIMEM_SRCS += editor/editor_encounters.c editor/editor_stamps.c torirsmaped/torirs_mapedctl_encounters.c

# The codecs on their own, content-free: the encounters document's parse ->
# emit byte-identical (comments, blank lines, CRLF, a missing final newline),
# its refusals with their line, format -> parse, the verbs' new text and
# their splices forward and back; the ledger's parse -> emit byte-identical
# over a generator-shaped file, an edit rewriting only its value (the note's
# column kept), a new key in the key column, `source = authored`, a removal,
# a new ledger; the derived max hit and combat level against hand-computed
# cases; the death-drop walk read into rows; the four ai templates and the
# step builder's ai trigger (model round trip, emitted script, conflict name).
.PHONY: test-editor-encounters
test-editor-encounters:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_encounters_test $(TORIMEM_SRCS) editor/editor_encounters.c \
		editor/editor_npcstats.c editor/editor_stamps.c editor/builder/builder_model.c \
		editor/builder/builder_text.c editor/builder/builder_emit.c editor/editor_cfg.c \
		editor/test/editor_encounters_test.c
	./$(OBJ_DIR)/editor_encounters_test

test-editor: test-editor-encounters

# The panel, headless, against the real chrome model and an embedded
# ToriRSMapEd over a SYNTHETIC content tree the test writes under
# $(OBJ_DIR)/tmp/editor_encounter (an npc with a cache block, a generated and
# an authored overlay, its stats ledger, a death-drop script, a set): the
# Set tab's rows and the selected npc's fields from its layers, Apply fields
# as one undo step into the authored overlay (a generated-only npc refused,
# the generated file untouched), the rings, Add at latched tile and Group...
# (a SPAWN and the set's line, one step), Colour; the Stats tab's inputs, its
# derived cells and their tooltips, Apply stats writing the ledger and the
# record in one step and its undo byte for byte, Save set; the Drops rows;
# the caster's refusal; the Test tab's chords and the fight log's filter; and
# torirsmapedctl's encounters / encounter-set / encounter-remove over a second
# connection to the same server (refusals, undo, save byte for byte). Links
# the client's objects minus main.o, as test-editor-panels does, plus the
# controller's verbs; no cache, no OSRS-Content.
.PHONY: test-editor-encounter-panel
test-editor-encounter-panel:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_encounter_panel_test editor/test/editor_encounter_panel_test.c \
		torirsmaped/torirs_mapedctl_encounters.c $(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/editor_encounter
	mkdir -p $(OBJ_DIR)/tmp
	./$(OBJ_DIR)/editor_encounter_panel_test $(OBJ_DIR)/tmp/editor_encounter

test-editor: test-editor-encounter-panel
