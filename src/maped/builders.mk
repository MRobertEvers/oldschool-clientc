# Phase 3, owner `builders` (docs/MAPED_IMPLEMENTATION.md section 1).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS and the content paths are defined: rule lines here name no
# such variable (it would expand empty), recipes use them freely.

# The interaction builders' model, emitters, splices, shell model and compile
# runner are plain data code. The panels (builder_panels.c) and their binding
# to the session (builder_session.c) register on editor_ui.c's registry
# (Editor_UiRegisterToolsPanels calls the three Register functions), so the
# client links both halves.
BUILDER_CORE_SRCS = editor/builder/builder_model.c editor/builder/builder_text.c \
	editor/builder/builder_emit.c editor/builder/builder_shell.c editor/builder/builder_ssc.c \
	editor/builder/builder_chat.c editor/builder/builder_lint.c
BUILDER_PANEL_SRCS = editor/builder/builder_panels.c editor/builder/builder_session.c
MAPED_CLIENT_SRCS += $(BUILDER_CORE_SRCS)
MAPED_CLIENT_SRCS += $(BUILDER_PANEL_SRCS)

# The builders end to end (src/editor/test/builder_test.c): the .interaction
# model round trip, golden emitter output (src/editor/test/golden/, rewritten
# with BUILDER_GOLDEN_UPDATE=1), dialogue structure, the NBSP lint, trigger
# conflicts, TEXT_SPLICE diffs, the multiloc/multinpc shell over every real
# shell in all.loc / all.npc (read-only), regenerate / save / undo / detach
# through an embedded ToriRSMapEd on a temp tree, the panels over the real
# chrome model, and a generated Talk-to compiled by sscompile against a stage
# copy of server/scripts. Everything written lands under $(OBJ_DIR)/tmp/builder;
# OSRS-Content is only read. The authority is linked the way torirsmaped is:
# with every fragment's MAPED_SERVER_SRCS (the config and interface codecs
# editor_cmd.c calls, the view launcher torirs_maped.c calls).
BUILDER_TEST_SRCS = $(BUILDER_CORE_SRCS) editor/builder/builder_panels.c \
	torirsmaped/torirs_maped.c torirsmaped/torirs_maped_buf.c torirsmaped/torirs_maped_cmdwire.c \
	torirsmaped/torirs_maped_embed.c editor/editor_host_local.c editor/editor_host_remote.c \
	editor/editor_doc.c editor/editor_cmd.c editor/editor_spawn.c \
	editor/editor_jm2.c ui/uitree_debug_overlay.c ui/torirs_chrome_exec.c \
	ui/torirs_chrome_exec_kind.c ui/torirs_chrome_rail.c engine/torirs_chrome_skin_baked.c engine/torirs_chrome_skin_tiers.c \
	editor/editor_hotkeys.c input/torirs_keymap.c editor/panels/edpanel_common.c \
	editor/test/builder_test.c
.PHONY: test-builder
test-builder: sscompile
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -UNDEBUG -UTORIRS_CHROME_EXEC_WEB_AVAILABLE \
		-UTORIRS_CHROME_EXEC_BROWSER_AVAILABLE -UTORIRS_CHROME_SURFACE_SDL \
		-o $(OBJ_DIR)/builder_test $(TORIMEM_SRCS) $(BUILDER_TEST_SRCS) $(MAPED_SERVER_SRCS) -lm $(MAPED_TEST_LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/builder/content $(OBJ_DIR)/tmp/builder/content_graph
	mkdir -p $(OBJ_DIR)/tmp/builder
	./$(OBJ_DIR)/builder_test $(EDITOR_CONTENT_DIR) $(abspath $(OBJ_DIR)/tmp/builder) \
		$(abspath $(SSC_BIN)) editor/test/golden

test-editor: test-builder
