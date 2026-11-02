# Phase 2, owner `edit-world` (docs/MAPED_IMPLEMENTATION.md section 1).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS, OBJS and the content paths are defined: rule lines here name
# no such variable (it would expand empty), recipes use them freely.

# The EDIT world layout's identity test (engine/world_builder/test/
# edit_world_test.c): a window of authored squares from the content tree, random
# edits through the EDIT layout, then a STATIC bake of the same edited squares
# -- same element models, vertex for vertex, at the same positions. Also prints
# one brush stroke's flush cost from the layout's own counters. Then the same
# check through the editor: the squares copied under $(OBJ_DIR)/tmp/edit_world,
# an embedded ToriRSMapEd serving them, edits as commands with undo and redo,
# carried to the world by the client's EDIT flush path. Links the
# client's objects minus main.o, as test-world-builder does (SRCS carries every
# fragment's MAPED_CLIENT_SRCS, so editor_doc.o finds editor_cfg.c there).
# Read-only over the content tree and the cache.
EDIT_WORLD_SEED ?= 12345
EDIT_WORLD_STROKES ?= 60
.PHONY: test-edit-world
test-edit-world:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/edit_world_test \
		engine/world_builder/test/edit_world_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/edit_world
	mkdir -p $(OBJ_DIR)/tmp/edit_world
	./$(OBJ_DIR)/edit_world_test $(EDITOR_CONTENT_DIR) $(EDITOR_CACHE_DIR) \
		$(EDIT_WORLD_SEED) $(EDIT_WORLD_STROKES) $(REPO_ROOT) $(OBJ_DIR)/tmp/edit_world

# The per-tile derive rides with the editor's tests.
test-editor: test-edit-world
