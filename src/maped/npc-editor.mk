# The NPC editor (docs/NPC_EDITOR_DESIGN.md).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS and the content paths are defined: rule lines here name no
# such variable (it would expand empty), recipes use them freely.

# The pure half: the server's mode rule applied to a record's statements
# (editor/editor_npc.c), the route and preview rectangles the World and the
# minimap draw. Links into every client build; the panels (the NPC editor in
# editor/builder/builder_panels.c, the Spawns preview) read it.
NPC_EDITOR_SRCS = editor/editor_npc.c
MAPED_CLIENT_SRCS += $(NPC_EDITOR_SRCS)
# The host-bound half: an npc's record across its layers as the editor
# writes it, the route and the ledger as one undo step
# (editor/panels/edpanel_npcdoc.c); builder_session.c binds it.
MAPED_CLIENT_SRCS += editor/panels/edpanel_npcdoc.c

# editor/test/editor_npc_test.c: the mode rule over the four shapes (patrol
# / wander / none / nomove), Hans's dead wanderrange, the coord and waypoint
# spellings both ways, the lap clock, the route rectangles, the sentence, the
# route as CFG_FIELD commands over a two-layer record, the .move ledger's
# line-preserving edit. No content tree, no chrome.
.PHONY: test-editor-npc
test-editor-npc:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_npc_test $(TORIMEM_SRCS) $(NPC_EDITOR_SRCS) editor/map_editor_ghost.c \
		editor/editor_cfg.c editor/editor_doc.c editor/editor_cmd.c editor/editor_jm2.c editor/editor_spawn.c \
		editor/test/editor_npc_test.c $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/editor_npc_test

test-editor: test-editor-npc
