# Workbench 6.5, part 1 (docs/MAPED_STATUS.md "Workbench step 6.5, part 1"):
# the SCENES document's codec (editor/editor_scenes.c), its commands through
# editor_cmd.c, the Scenes panel (editor/panels/edpanel_scenes.c), and the
# Lanes panel's scratch lane and switch (editor/panels/edpanel_publish.c,
# lanes.mk's).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS, OBJS and the content paths are defined: rule lines here name
# no such variable (it would expand empty), recipes use them freely.

# editor_cmd.c applies SCENE_* through the codec, so every binary that links
# editor_cmd.c links editor_scenes.c -- by palette.mk's routes: the client and
# torirsmaped through these lists...
MAPED_CLIENT_SRCS += editor/editor_scenes.c
MAPED_SERVER_SRCS += editor/editor_scenes.c
# The Scenes panel.
MAPED_CLIENT_SRCS += editor/panels/edpanel_scenes.c

# ...and the tests that list their sources by hand (areas.mk's list).
test-editor-cmd test-editor-save test-torirsmaped-embed test-editor-cfg test-editor-alloc \
test-editor-vars test-editor-if torirsmapedctl: \
	TORIMEM_SRCS += editor/editor_scenes.c

# The codec on its own, content-free: parse -> emit byte-identical (comments,
# blank lines, CRLF, a missing final newline), the grammar's refusals with
# their line (a segment kind a scene holds twice, a second header, a bad
# camera / level / preview / account), format -> parse for every segment and
# the camera's fine units, the plans (save a new scene, update in place keeping
# the first line's place and an unchanged line, rename, delete) applied and
# undone byte for byte, the header an empty file gains and loses, a stale
# command refused, the History labels, and the v2 wire form of both kinds.
.PHONY: test-editor-scenes
test-editor-scenes:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_scenes_test $(TORIMEM_SRCS) editor/editor_scenes.c \
		torirsmaped/torirs_maped_cmdwire.c torirsmaped/torirs_maped_buf.c \
		editor/test/editor_scenes_test.c
	./$(OBJ_DIR)/editor_scenes_test

test-editor: test-editor-scenes

# The panels, headless, against the real chrome model and an embedded
# ToriRSMapEd on a lane of a SYNTHETIC tree the test writes under
# $(OBJ_DIR)/tmp/editor_scenes (a repo root with build/lanes, one
# hand-written square, an editor/areas.txt): Save scene from the window's
# state (squares, the World's camera, the pinned level and Above, the var
# preview, the lane, the account) as one undo step and saved at once; a
# second window of the Client seeing it after a pump and entering it (its
# squares opened, the camera put, the level pinned, STATE_VAR_PREVIEW
# published, the teleport refused while the lane is down); Update rewriting
# it in place; Rename and Delete as one step each; the editor restarted and
# the scene entered again with the same squares, camera, level and preview;
# the scratch lane's name rule and Ctrl+Shift+N making it from main and
# staging the switch (build/lanes/lane.ini); the Lanes panel's list with
# change counts; the status bar's lane chip ("work | 1 changed") and its
# click opening the Lanes panel; the command palette's scene rows. Links the
# client's objects minus main.o, as test-editor-panels does; no cache, no
# OSRS-Content.
.PHONY: test-editor-scenes-ops
test-editor-scenes-ops:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_scenes_ops_test editor/test/editor_scenes_ops_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/editor_scenes
	mkdir -p $(OBJ_DIR)/tmp
	./$(OBJ_DIR)/editor_scenes_ops_test $(abspath $(OBJ_DIR)/tmp/editor_scenes)

test-editor: test-editor-scenes-ops
