# Gesture frames, the consumers' half (workbench 6.1, design section 14): the
# chrome routes them (ui/uitree_debug_overlay_gesture.u.c, tested in
# test-chrome-widgets), the World camera takes the rest
# (editor/editor_world_gesture.c, the pure half; app/app_map_editor.c the
# glue), the picker and the Preview take theirs through the chrome's events.
#
# input/torirs_gesture_stub.c answers "no frames" until the bus half lands
# (input/torirs_gesture.h is the contract); the integrator deletes it.
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR and CFLAGS exist: rule lines here name no such variable, recipes
# use them freely.
MAPED_CLIENT_SRCS += input/torirs_gesture_stub.c editor/editor_world_gesture.c

# The World camera's pure helper against fake frame lists: a two-finger pan
# keeps the ground under the fingers, a pinch keeps its point under it and
# scales the distance by the ratio, a twist keeps the look-at point centred,
# one finger is never the camera's, and a recorded pinch replays to the same
# pose. No renderer, no content.
.PHONY: test-editor-world-gesture
test-editor-world-gesture:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -UNDEBUG -o $(OBJ_DIR)/editor_world_gesture_test editor/editor_world_gesture.c \
		editor/test/editor_world_gesture_test.c $(TORIDRAW_DIR)/graphics/shared_tables.c \
		$(TORIDRAW_DIR)/toridraw_math.c -lm
	./$(OBJ_DIR)/editor_world_gesture_test

test-editor: test-editor-world-gesture
