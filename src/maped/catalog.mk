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
		torirsmaped/test/catalog_test.c -lm
	rm -rf $(OBJ_DIR)/tmp/catalog
	mkdir -p $(OBJ_DIR)/tmp/catalog
	./$(OBJ_DIR)/catalog_test $(EDITOR_CONTENT_DIR) $(OBJ_DIR)/tmp/catalog

EDITOR_TREE_TESTS += test-catalog

# The catalog's work bounds (docs/MAPED_STATUS.md "Catalog performance"):
# over a synthetic 60,000-loc tree written under $(OBJ_DIR)/tmp/catalog_perf,
# a query, a record and an idle request must not re-stat the tree; the
# freshness sweep advances in budgeted slices on the service pass and still
# catches an edit; a record's file is opened once per pick. Then, given the
# real content tree, the same numbers printed for the record (not checked:
# they are the machine's).
.PHONY: test-catalog-perf
test-catalog-perf:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/catalog_perf_test $(TORIMEM_SRCS) \
		torirsmaped/torirs_maped.c torirsmaped/torirs_maped_buf.c \
		torirsmaped/torirs_maped_cmdwire.c torirsmaped/torirs_maped_embed.c \
		editor/editor_host_local.c editor/editor_host_remote.c editor/editor_doc.c \
		editor/editor_cmd.c editor/editor_spawn.c editor/editor_jm2.c \
		$(filter-out torirsmaped/%,$(MAPED_SERVER_SRCS)) \
		editor/test/editor_catalog_perf_test.c
	rm -rf $(OBJ_DIR)/tmp/catalog_perf
	mkdir -p $(OBJ_DIR)/tmp/catalog_perf
	./$(OBJ_DIR)/catalog_perf_test $(OBJ_DIR)/tmp/catalog_perf $(EDITOR_CONTENT_DIR)

test-editor: test-catalog-perf

# The catalog as a loc finder (workbench 6.2), over a tiny tree written under
# $(OBJ_DIR)/tmp/catalog_finder, no content needed: filter values narrow as
# a prefix in a documented order, `shape=` classes, the derived loc fields
# (shapes, size, layer), and the PROGRESS tail only when asked for.
.PHONY: test-catalog-finder
test-catalog-finder:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/catalog_finder_test $(TORIMEM_SRCS) \
		torirsmaped/torirs_maped.c torirsmaped/torirs_maped_buf.c \
		torirsmaped/torirs_maped_cmdwire.c torirsmaped/torirs_maped_embed.c \
		editor/editor_host_local.c editor/editor_host_remote.c editor/editor_doc.c \
		editor/editor_cmd.c editor/editor_spawn.c editor/editor_jm2.c \
		$(filter-out torirsmaped/%,$(MAPED_SERVER_SRCS)) \
		torirsmaped/test/catalog_finder_test.c
	rm -rf $(OBJ_DIR)/tmp/catalog_finder
	mkdir -p $(OBJ_DIR)/tmp/catalog_finder
	./$(OBJ_DIR)/catalog_finder_test $(OBJ_DIR)/tmp/catalog_finder

test-editor: test-catalog-finder
