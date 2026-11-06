# Workbench step 6.3, part 1 (design section 7 "Placing and moving locs"):
# the Place and Select tools' direct manipulation -- wall runs and tile runs,
# the marquee, select same, Fit in the shared tools, refusals said in the
# status bar. Content-free: synthetic squares written under $(OBJ_DIR)/tmp,
# an embedded ToriRSMapEd serving them, the world-free halves of the World's
# drags (editor/panels/edpanel_ops.h, edp_run_* and edp_select_*) driven as
# the drag drives them. Links the client's objects minus main.o, as
# test-editor-groups does. The STATIC-rebake check over real squares is in
# test-edit-world (it needs the content tree).
#
# Included BEFORE OBJ_DIR and CFLAGS exist (see tools-panels.mk): rule lines
# here name no such variable, recipes use them freely.

.PHONY: test-editor-placement
test-editor-placement:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_placement_test editor/test/editor_placement_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS) $(MAPED_TEST_LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/placement
	mkdir -p $(OBJ_DIR)/tmp
	./$(OBJ_DIR)/editor_placement_test $(abspath $(OBJ_DIR)/tmp/placement)

test-editor: test-editor-placement
