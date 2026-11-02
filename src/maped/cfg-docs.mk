# Phase 2, owner `cfg-docs` (docs/MAPED_IMPLEMENTATION.md section 1).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS, INI_SRC and the content paths are defined: rule lines here
# name no such variable (it would expand empty), recipes use them freely.

# editor_doc.c's Editor_CfgApply is the config codec now, so every binary that
# links editor_doc.c links editor_cfg.c. The client and torirsmaped take it
# through the hooks phase 1 left for fragments.
MAPED_CLIENT_SRCS += editor/editor_cfg.c
MAPED_SERVER_SRCS += editor/editor_cfg.c

# SAVE_AS (a config record copied to a new name, torirs_maped.c
# handle_save_as) allocates the id with editor_alloc.c, which reads the
# namespace floors from the content register. Every binary that links
# torirs_maped.c links them: torirsmaped and the tests through
# MAPED_SERVER_SRCS, the client through MAPED_CLIENT_SRCS (it already links
# ini.o; with EMBED_SERVER=1 the server's own list carries the register).
MAPED_SERVER_SRCS += editor/editor_alloc.c content/content_register.c $(INI_SRC)
MAPED_CLIENT_SRCS += editor/editor_alloc.c
ifneq ($(EMBED_SERVER),1)
MAPED_CLIENT_SRCS += content/content_register.c
endif
test-torirsmaped-embed: TORIMEM_SRCS += editor/editor_alloc.c content/content_register.c $(INI_SRC)

# The editor tests in src/makefile list their sources by hand, and that file is
# closed to this owner. A target-specific append puts the codec on exactly
# those link lines (each recipe names $(TORIMEM_SRCS)). If the integrator adds
# editor/editor_cfg.c to these recipes in src/makefile instead, delete this
# line, or the codec links twice.
test-editor-cmd test-editor-jm2 test-editor-derive test-editor-save test-torirsmaped-embed: \
	TORIMEM_SRCS += editor/editor_cfg.c

# The codec: the byte-exact round trip over every configs/all.* and every
# server/scripts/**/configs/*.{loc,npc,obj,varp,varbit,enum,struct,param,inv}
# (plus the typed files elsewhere and the lanes' configs, counted apart), an
# edit-and-undo probe on real records, the edit operations, and CFG_FIELD /
# CFG_BLOCK through Editor_CmdApply and the undo stack. Read-only.
.PHONY: test-editor-cfg
test-editor-cfg:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_cfg_test $(TORIMEM_SRCS) \
		editor/editor_cfg.c editor/editor_doc.c editor/editor_cmd.c editor/editor_jm2.c \
		editor/editor_spawn.c editor/test/editor_cfg_test.c
	./$(OBJ_DIR)/editor_cfg_test $(EDITOR_CONTENT_DIR)

test-editor: test-editor-cfg

# Id allocation against tools/ss_allocate.py itself: the oracle imports the
# script and prints, per namespace, what it would allocate and the ledger bytes
# write_pack would leave (under $(OBJ_DIR)/tmp, never the tree); the test loads
# the same namespaces from the tree and must match every number and, by
# applying its commands to the ledger's text, every byte. Read-only.
EDITOR_ALLOC_TEST_NAMESPACES = enum struct dbtable dbrow param mesanim inv varn varp vars \
	npc obj loc seq spotanim varbit varc category 3_interfaces
.PHONY: test-editor-alloc
test-editor-alloc:
	rm -rf $(OBJ_DIR)/tmp/editor_alloc
	mkdir -p $(OBJ_DIR)/tmp/editor_alloc
	python3 -I editor/test/editor_alloc_oracle.py $(REPO_ROOT)/tools $(EDITOR_CONTENT_DIR) \
		$(OBJ_DIR)/tmp/editor_alloc editor_alloc_probe $(EDITOR_ALLOC_TEST_NAMESPACES) \
		> $(OBJ_DIR)/tmp/editor_alloc/oracle.txt
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_alloc_test $(TORIMEM_SRCS) \
		editor/editor_alloc.c editor/editor_cfg.c editor/editor_doc.c editor/editor_cmd.c \
		editor/editor_jm2.c editor/editor_spawn.c content/content_register.c $(INI_SRC) \
		editor/test/editor_alloc_test.c
	./$(OBJ_DIR)/editor_alloc_test $(EDITOR_CONTENT_DIR) $(OBJ_DIR)/tmp/editor_alloc/oracle.txt \
		$(OBJ_DIR)/tmp/editor_alloc

test-editor: test-editor-alloc

# Varps and varbits: names, the bit map, the overlap check, the new-varbit
# commands, and the real tree's varbits (overlaps the cache really has are
# listed, not failed). Read-only.
.PHONY: test-editor-vars
test-editor-vars:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_vars_test $(TORIMEM_SRCS) \
		editor/editor_vars.c editor/editor_cfg.c editor/editor_doc.c editor/editor_cmd.c \
		editor/editor_jm2.c editor/editor_spawn.c editor/test/editor_vars_test.c
	./$(OBJ_DIR)/editor_vars_test $(EDITOR_CONTENT_DIR)

test-editor: test-editor-vars
