# Lane records in the editor's client (docs/MAPED_STATUS.md, 2026-11-11): a
# loc / npc / obj made by Copy to new is defined from its text
# (editor/editor_recdef.c, the cachepack packers' key tables over the rscache
# decode structs and the cache loaders' own adaptors) and handed to the cache
# provider (editor/editor_lanerecs.c), so the World draws, places and selects
# it with no Save, RUN or restart in between.
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS, OBJS and the content paths are defined: rule lines here name
# no such variable (it would expand empty), recipes use them freely.

MAPED_CLIENT_SRCS += editor/editor_recdef.c editor/editor_lanerecs.c

# The decoder, content-free (editor/test/editor_recdef_test.c): a loc, an npc
# and an obj block as configs/all.<type> writes them, decoded and compared
# field by field with what the cache's own decoder and adaptor make of the
# same record -- built here by rscache's ENCODER from a struct stated the way
# cachepack states it, so the two paths are held to one answer; the names a
# block spells through a resolver; `default`, `hidden`, the walk flags, the
# two model-table forms; a bad value counted and the field left at its
# default; an empty block is no record; the ledger's id=name lookup.
.PHONY: test-editor-recdef
test-editor-recdef:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(OBJ_DIR)/hmap.o $(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o
	$(CC) $(CFLAGS) -UNDEBUG -o $(OBJ_DIR)/editor_recdef_test$(PLATFORM_EXE_SUFFIX) $(TORIMEM_SRCS) \
		editor/test/editor_recdef_test.c editor/editor_recdef.c \
		engine/torirs_types.c engine/torirs_location_from_rscache.c \
		engine/torirs_npctype_from_rscache.c engine/torirs_objtype_from_rscache.c \
		$(OBJ_DIR)/rscache_unity.o $(OBJ_DIR)/bzip.o $(OBJ_DIR)/bzip_encode.o \
		$(OBJ_DIR)/miniz.o $(OBJ_DIR)/xteas.o $(OBJ_DIR)/hmap.o $(LDFLAGS) $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/editor_recdef_test$(PLATFORM_EXE_SUFFIX)

test-editor: test-editor-recdef
