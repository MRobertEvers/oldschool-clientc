# Gesture frames, the consumers' half (workbench 6.1, design section 14): the
# chrome routes them (ui/uitree_debug_overlay_gesture.u.c, tested in
# test-chrome-widgets), the World camera takes the rest
# (editor/editor_world_gesture.c, the pure half; app/app_map_editor.c the
# glue), the picker and the Preview take theirs through the chrome's events.
#
# The frames come from input/torirs_input.c's decoded list
# (ToriRS_InputGestures, input/torirs_gesture.h), which the client links.
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR and CFLAGS exist: rule lines here name no such variable, recipes
# use them freely.
MAPED_CLIENT_SRCS += editor/editor_world_gesture.c

# The World camera's pure helper against fake frame lists: a two-finger pan
# keeps the ground under the fingers, a pinch keeps its point under it and
# scales the distance by the ratio, a twist keeps the look-at point centred,
# one finger is never the camera's, and a recorded pinch replays to the same
# pose (recorded off the command bus, replayed from the file through
# ToriRS_Input_ApplyCmd). No renderer, no content.
.PHONY: test-editor-world-gesture
test-editor-world-gesture:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -UNDEBUG -o $(OBJ_DIR)/editor_world_gesture_test editor/editor_world_gesture.c \
		editor/test/editor_world_gesture_test.c $(TORIDRAW_DIR)/graphics/shared_tables.c \
		$(TORIDRAW_DIR)/toridraw_math.c $(TORIMEM_SRCS) input/torirs_input.c input/torirs_input_cmd.c \
		cmd/cmdbus.c cmd/cmdring.c $(LDFLAGS) -lm
	./$(OBJ_DIR)/editor_world_gesture_test $(OBJ_DIR)/editor_world_gesture_test.trscmd

test-editor: test-editor-world-gesture
