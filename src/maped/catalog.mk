# Phase 2, owner `catalog` (docs/MAPED_IMPLEMENTATION.md section 1).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS and the content paths are defined: rule lines here name no
# such variable (it would expand empty), recipes use them freely.
#
# No source hook: torirsmaped/torirs_maped_catalog.c is compiled INTO
# torirsmaped/torirs_maped.c (an #include at its end), so every link line that
# already names torirs_maped.c — the client with or without EMBED_SERVER, the
# embed test, the daemon — carries the catalog without an edit. Never add the
# catalog .c to a link line next to torirs_maped.c.

# The catalog over the real content tree, READ ONLY, through the API: the index
# build time, every name in every pack/<t>.pack, pack/<ns>.alloc,
# configs/all.<type>.compack and interfaces/<name>.compack found by exact
# search, the square atlas against maps/ and the lanes. Then a small tree
# written under $(OBJ_DIR)/tmp/catalog through the embedded server: query
# paging and filters, record provenance, the atlas and its broadcast after
# DOC_CREATE and Save, and the mtime refresh.
.PHONY: test-catalog
test-catalog:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/catalog_test $(TORIMEM_SRCS) \
		torirsmaped/torirs_maped.c torirsmaped/torirs_maped_buf.c \
		torirsmaped/torirs_maped_cmdwire.c torirsmaped/torirs_maped_embed.c \
		editor/editor_host_local.c editor/editor_host_remote.c editor/editor_doc.c \
		editor/editor_cmd.c editor/editor_spawn.c editor/editor_jm2.c \
		$(filter-out torirsmaped/%,$(MAPED_SERVER_SRCS)) \
		torirsmaped/test/catalog_test.c
	rm -rf $(OBJ_DIR)/tmp/catalog
	mkdir -p $(OBJ_DIR)/tmp/catalog
	./$(OBJ_DIR)/catalog_test $(EDITOR_CONTENT_DIR) $(OBJ_DIR)/tmp/catalog

test-editor: test-catalog
