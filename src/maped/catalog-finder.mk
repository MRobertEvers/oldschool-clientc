# Workbench 6.2, the Catalog as a loc finder (design section 6; the client
# half): the cells list's rows, filter and shortcut chips, the favourites
# document, recent picks, the continuous window, the sort, the shape keycap
# pick, Pick from world, and the Preview pane that follows the right thing.
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS and OBJS exist: rule lines here name no such variable,
# recipes use them freely.

# The FAVOURITES codec (a TEXT document's grammar: editor_favourites.h) and
# the chip strips the Catalog and the Preview draw (edpanel_chips.c).
MAPED_CLIENT_SRCS += editor/editor_favourites.c editor/panels/edpanel_chips.c

# Content-free: the catalog model fed synthetic rows (chips composing into
# the filter text, the shapes read from summary fields, the keycap pick, the
# continuous window and its prefetch, the sort, the local lists, recents, the
# preview-follows rule), the favourites codec (round trip byte for byte, the
# splices forward and back), then the registered Catalog and Preview over an
# embedded ToriRSMapEd on a synthetic tree written under
# $(OBJ_DIR)/tmp/editor_catalog_finder (a synthetic all.loc with shape lines,
# no OSRS-Content): typing a chip, scrolling to a prefetched page, a keycap
# click, the star into editor/favourites.txt and its undo, recents, Pick from
# world. Links the client's objects minus main.o, as test-editor-chords does;
# builds struct Editor on the heap.
.PHONY: test-editor-catalog-finder
test-editor-catalog-finder:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_catalog_finder_test editor/test/editor_catalog_finder_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/editor_catalog_finder
	mkdir -p $(OBJ_DIR)/tmp
	./$(OBJ_DIR)/editor_catalog_finder_test $(OBJ_DIR)/tmp/editor_catalog_finder

test-editor: test-editor-catalog-finder
