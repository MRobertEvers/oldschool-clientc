# Phase 7d, the Triggers panel (docs/MAPED_STATUS.md row 7d).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR and CFLAGS exist: rule lines here name no such variable, recipes
# use them freely.
#
# editor/editor_script.c is the pure half (trigger headers, the op / ap kinds
# a loc, npc or obj carries, the starter template, the "add trigger" plan, the
# compile diagnostics per line; editor_open_cmd.h, header-only, the "Open in
# editor" command line); editor/panels/edpanel_triggers.c is the Triggers
# panel (module "triggers"). The spawn of the user's editor is
# Editor_HostLocalOpenInEditor in editor/editor_host_local.c.
MAPED_CLIENT_SRCS += editor/editor_script.c editor/panels/edpanel_triggers.c

# The pure half on its own: header parsing (CRLF, a last line with no break,
# labels and procs, non-headers), every kind of every type and its menu field,
# the template for every kind, the add plan (which field, which file, the
# appended text, the header's line, each refusal), the open-in-editor command
# line (placeholders, shell quoting, the defaults), and a fake sscompile output
# through Builder_SscParseOutput mapped onto document lines. With the optional
# arguments (an sscompile and the content tree) it also compiles every
# template against a stage under $(OBJ_DIR)/tmp/editor_script: OSRS-Content is
# only read.
.PHONY: test-editor-script
test-editor-script: sscompile
	mkdir -p $(OBJ_DIR)/tmp/editor_script
	$(CC) $(CFLAGS) -UNDEBUG -o $(OBJ_DIR)/editor_script_test $(TORIMEM_SRCS) editor/editor_script.c \
		editor/builder/builder_ssc.c editor/test/editor_script_test.c
	./$(OBJ_DIR)/editor_script_test $(abspath $(SSC_BIN)) $(EDITOR_CONTENT_DIR) \
		$(abspath $(OBJ_DIR)/tmp/editor_script)

test-editor: test-editor-script
