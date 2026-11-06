# Workbench step 6.4, the picker's navigation and panes (design section 5,
# "Navigation"): Find's parser and resolver (edpanel_picker_find.c), the
# focused cell, the open set, the strip's collapse rule, Recent and text on a
# track (edpanel_picker_nav.c), and the Squares panel that drives them
# (edpanel_squares.c, in tools-panels.mk).
#
# Included BEFORE OBJ_DIR and CFLAGS exist (see tools-panels.mk): rule lines
# here name no such variable, recipes use them freely.
MAPED_CLIENT_SRCS += editor/panels/edpanel_picker_find.c editor/panels/edpanel_picker_nav.c

# Content-free: a synthetic atlas (the surface, a dungeon, Unmapped, a lane,
# the instance space) handed to the registered Squares panel over the real
# chrome model, as test-editor-chords boots it. Find over every form, the
# arrows over the atlas (and the K rows they resolve through), the strip's
# collapse rule and its chips, the detail pane's rows measured against their
# tracks, Recent, the Area dropdown's lane and instance rows. Links the
# client's objects minus main.o.
.PHONY: test-editor-picker
test-editor-picker:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_picker_test editor/test/editor_picker_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS) $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/editor_picker_test

test-editor: test-editor-picker
