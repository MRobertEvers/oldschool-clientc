# The interface DOCUMENT codec (editor/editor_if.{c,h}, editor/editor_if_typed.c):
# a `.if` file as an IFACE document, its IF_FIELD / IF_COMPONENT commands, and
# the codec's own test. That is all this fragment builds. The interface editor
# and the CS2 tools it also carried (their panels under editor/, their App
# glue and their headless test) were removed on 2026-11-09: editing interfaces
# is a tool of its own, which will link this codec; the map editor only keeps
# the document kind and its wire encoding.
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS and the content paths are defined: rule lines here name no
# such variable (it would expand empty), recipes use them freely.

# editor_cmd.c's IF arms are the interface codec (Editor_IfDocApply), so every
# binary that links editor_cmd.c links editor_if.c: the client and torirsmaped
# both. The typed layer (editor_if_typed.c: a block to an RSCache_Dat2Component
# and back) has no caller in either since the interface editor went; it links
# rscache's component type, so it stays off both lists and is named on
# test-editor-if's link line, which holds it to cachepack byte for byte.
MAPED_CLIENT_SRCS += editor/editor_if.c
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
