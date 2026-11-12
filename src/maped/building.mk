# The Building tool (docs/MAPED_STATUS.md 2026-11-11): the planner
# (editor/editor_building.c: a dragged rectangle as walls, corners, floor,
# roof flag, roof, storeys and a ladder, by the rules the content's own
# houses follow), its document ops (editor/panels/edpanel_ops_building.c:
# the commands, the door and window swaps) and its panel
# (editor/panels/edpanel_building.c, module "building").
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS, OBJS and the content paths are defined: rule lines here name
# no such variable (it would expand empty), recipes use them freely.
MAPED_CLIENT_SRCS += editor/editor_building.c editor/panels/edpanel_ops_building.c \
	editor/panels/edpanel_building.c

# The planner and the ops, headless, against an in-memory document of two
# empty squares (no cache, no OSRS-Content): the 9x7 cottage's pieces at the
# shapes and rotations m50_50's houses have them, narrow footprints, the
# shape-mask fallbacks, storeys and the ladder, every refusal; the commands
# applied through Editor_CmdApply and undone; a door and back, a window's two
# faces and off, a wall found from the floor beside it; the wall line; the
# same footprint built a storey taller. Links the client's objects minus
# main.o, as test-editor-chords does.
.PHONY: test-editor-building
test-editor-building:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_building_test editor/test/editor_building_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS) $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/editor_building_test

test-editor: test-editor-building
