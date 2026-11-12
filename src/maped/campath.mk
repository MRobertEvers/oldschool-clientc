# The Camera path panel (docs/MAPED_STATUS.md "Camera paths"): a cutscene's
# scripted camera authored in the World -- the document's codec, the eye <->
# keyframe maths, the client's scripted camera as a stepped simulation and
# the proc Copy script renders (editor/editor_campath.c).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS, OBJS and the content paths are defined: rule lines here name
# no such variable (it would expand empty), recipes use them freely.

# The document is a TEXT document written with TEXT_SPLICE commands
# (editor_stamps.c plans them): editor_cmd.c never calls the codec, so only
# the client links it.
MAPED_CLIENT_SRCS += editor/editor_campath.c
# The panel (module "campath"), and its App glue: the World camera read and
# put exactly, the ground under a point, the shown path's overlay.
MAPED_CLIENT_SRCS += editor/panels/edpanel_campath.c app/app_editor_campath.c

# Pure, content-free: parse -> emit byte-identical (comments, blank lines,
# CRLF, a missing final newline) and the grammar's refusals with their line;
# the verbs' new text (a first path bringing the header, a keyframe added in
# place, a rename, a delete taking the header back); eye -> keyframe against
# hand-computed cases, on a slope, and from 500 poses over uneven ground with
# the subject held; the client's ease digit for digit, a cut, a glide's cycle
# count; the proc in world tiles, instance-relative, unlocked and framed, byte
# for byte.
.PHONY: test-editor-campath
test-editor-campath:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_campath_test $(TORIMEM_SRCS) editor/editor_campath.c \
		editor/test/editor_campath_test.c $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/editor_campath_test

test-editor: test-editor-campath

# The panel, headless, against the real chrome model and an embedded
# ToriRSMapEd over a SYNTHETIC content tree the test writes under
# $(OBJ_DIR)/tmp/editor_campath, with a World camera and a flat ground
# standing in for the App's: New path and its refusals; Add keyframe fitting
# the camera, writing the `key` line as one undo step and putting the camera
# on it; Update, Go to, Move up / down, Delete; Hold and the rate pairs by
# Enter and by leaving the field, a refused value put back; Move and Speed;
# Coordinates, Template and the cutscene frame; the proc the panel shows
# (unlocked when a keyframe is steep); Preview flown on the chrome's clock and
# the camera given back, Stop, a hitch; the World's marks; Save all writing
# editor/campaths.txt; Rename, Delete path, Undo back to no text. Links the
# client's objects minus main.o, as test-editor-instance-ops does; no cache, no
# OSRS-Content.
.PHONY: test-editor-campath-panel
test-editor-campath-panel:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_campath_panel_test editor/test/editor_campath_panel_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS) $(MAPED_TEST_LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/editor_campath
	mkdir -p $(OBJ_DIR)/tmp
	./$(OBJ_DIR)/editor_campath_panel_test $(OBJ_DIR)/tmp/editor_campath

test-editor: test-editor-campath-panel
