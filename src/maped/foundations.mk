# Phase 1, owner `foundations` (docs/MAPED_IMPLEMENTATION.md section 1).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS and the content paths are defined: rule lines here name no
# such variable (it would expand empty), recipes use them freely.

# The `.spawn` codec: edit operations, and the byte-exact round trip over every
# generated spawn file (server/scripts/areas/world/configs, ~970 files) plus
# every hand-authored one elsewhere under server/scripts. Read-only over the
# content tree.
.PHONY: test-spawn-codec
test-spawn-codec:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_spawn_test $(TORIMEM_SRCS) \
		editor/editor_spawn.c editor/test/editor_spawn_test.c $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/editor_spawn_test \
		$(EDITOR_CONTENT_DIR)/server/scripts/areas/world/configs \
		$(EDITOR_CONTENT_DIR)/server/scripts

# The server loader's suppression row (`-name x z level`), through
# ToriRSServer_ContentLoadSpawns on a temp two-lane spawn tree written under
# $(OBJ_DIR)/tmp. Links the content loader the way ToriRSServer_Pack does,
# minus that tool's main().
SPAWN_SUPPRESS_TEST_OBJS = rscache_unity.o bzip.o bzip_encode.o miniz.o xteas.o
.PHONY: test-spawn-suppress
test-spawn-suppress:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(addprefix $(OBJ_DIR)/,$(SPAWN_SUPPRESS_TEST_OBJS))
	$(CC) $(CFLAGS) -I$(RSCACHE_DIR)/tools/cachepack \
		-o $(OBJ_DIR)/spawn_suppress_test $(TORIMEM_SRCS) \
		$(filter-out torirsserver/torirs_server_pack.c,$(TORIRSSERVER_PACK_SRCS)) \
		torirsserver/test/spawn_suppress_test.c \
		$(addprefix $(OBJ_DIR)/,$(SPAWN_SUPPRESS_TEST_OBJS)) -lm $(MAPED_TEST_LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/spawn_suppress
	mkdir -p $(OBJ_DIR)/tmp/spawn_suppress
	./$(OBJ_DIR)/spawn_suppress_test $(OBJ_DIR)/tmp/spawn_suppress

# Both run with the rest of the editor's tests.
EDITOR_TREE_TESTS += test-spawn-codec
test-editor: test-spawn-suppress
