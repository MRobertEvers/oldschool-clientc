# Phase 3, owner `iface-cs2` (docs/MAPED_IMPLEMENTATION.md section 1).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS and the content paths are defined: rule lines here name no
# such variable (it would expand empty), recipes use them freely.

# editor_cmd.c's IF arms are the interface codec now (Editor_IfDocApply), so
# every binary that links editor_cmd.c links editor_if.c. The typed layer
# (editor_if_typed.c) needs rscache's component type, which the client has and
# torirsmaped does not; the interface editor's modules and the app glue are
# client-only.
MAPED_CLIENT_SRCS += editor/editor_if.c editor/editor_if_typed.c \
	editor/iface/iface_model.c editor/iface/iface_cs2.c editor/iface/iface_panels.c \
	editor/iface/iface_register.c app/app_iface_editor.c
MAPED_SERVER_SRCS += editor/editor_if.c

# The editor tests in src/makefile and the other fragments list their sources
# by hand. A target-specific append puts the codec on exactly those link lines
# (each recipe names $(TORIMEM_SRCS)). If the integrator names editor/editor_if.c
# in those recipes instead, delete this line, or the codec links twice.
# (test-catalog and test-builder link $(MAPED_SERVER_SRCS), which already
# carries it.)
test-editor-cmd test-editor-save test-torirsmaped-embed test-editor-cfg test-editor-alloc \
test-editor-vars: \
	TORIMEM_SRCS += editor/editor_if.c

# The codec over all 982 interfaces: byte-exact index, cachepack's preamble,
# the vocabulary in canonical order, layer= as (interface << 16) | child, the
# type= set, every block through the typed layer and back, every hook
# re-formatted, positional compacks, and per interface an edit-and-undo probe
# of each command shape. Then IF_FIELD / IF_COMPONENT through Editor_CmdApply.
# Read-only.
IFACE_TEST_RSCACHE_OBJS = rscache_unity.o bzip.o bzip_encode.o miniz.o xteas.o
.PHONY: test-editor-if
test-editor-if:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(addprefix $(OBJ_DIR)/,$(IFACE_TEST_RSCACHE_OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_if_test $(TORIMEM_SRCS) \
		editor/editor_if.c editor/editor_if_typed.c editor/editor_cfg.c editor/editor_doc.c \
		editor/editor_cmd.c editor/editor_jm2.c editor/editor_spawn.c \
		editor/test/editor_if_test.c \
		$(addprefix $(OBJ_DIR)/,$(IFACE_TEST_RSCACHE_OBJS)) -lm $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/editor_if_test $(EDITOR_CONTENT_DIR)

EDITOR_TREE_TESTS += test-editor-if

# The interface editor's modules, headless, linked against the client's own
# objects (like test-edit-world): the model over bankmain, every interface's
# document as the client pack it mounts and as the cache emits it, the CS2
# script catalog and hooks, a real `cs2 compile` (and a declined one) under
# $(OBJ_DIR)/tmp/iface_editor, CODE edits as splices, watch/harness, and the
# panels on a real ToriRSChrome. Reads OSRS-Content and the cache only.
IFACE_EDITOR_RUNESTAR ?= $(HOME)/Documents/git_repos/cs2/src/main/resources/org/runestar/cs2
IFACE_EDITOR_CACHE ?= $(REPO_ROOT)/cache.osrs241
.PHONY: test-iface-editor
test-iface-editor:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_iface_test editor/test/editor_iface_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS) $(MAPED_TEST_LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/iface_editor
	mkdir -p $(OBJ_DIR)/tmp/iface_editor
	./$(OBJ_DIR)/editor_iface_test $(EDITOR_CONTENT_DIR) $(IFACE_EDITOR_CACHE) $(abspath $(CS2_BIN)) \
		$(if $(wildcard $(IFACE_EDITOR_RUNESTAR)),$(IFACE_EDITOR_RUNESTAR),-) \
		$(abspath $(OBJ_DIR)/tmp/iface_editor)

EDITOR_TREE_TESTS += test-iface-editor
