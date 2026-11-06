# Phase 3, owner `views` (docs/MAPED_IMPLEMENTATION.md section 1).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS and the content paths are defined: rule lines here name no
# such variable (it would expand empty), recipes use them freely.
#
# No source hook for the server half: torirsmaped/torirs_maped_view.c is
# compiled INTO torirsmaped/torirs_maped.c (an #include at its end, like the
# catalog), so every link line that names torirs_maped.c carries SPAWN_VIEW,
# the camera keys and STATE_VIEW_SELF. Never add it to a link line.

# The viewer window's toolbar, per-view state and camera publish/follow, in
# the client.
MAPED_CLIENT_SRCS += editor/editor_view.c
# Its App glue (EditorView_App*): a unit of the App layer, so the core above
# names no App (tools/appc_map.py check).
MAPED_CLIENT_SRCS += app/app_editor_view.c
# The daemon binds SPAWN_VIEW to the launch service's `view` verb. The client
# already links launch_sessions.c (src/makefile SRCS).
MAPED_SERVER_SRCS += platform/launch_sessions.c
# The loopback front door (listening socket + per-connection transport): the
# daemon's reactor, and the embedded server's on-demand socket that a
# popped-out panel window dials (torirs_maped_net.h).
MAPED_SERVER_SRCS += torirsmaped/torirs_maped_net.c
MAPED_CLIENT_SRCS += torirsmaped/torirs_maped_net.c

# Three connections on one embedded ToriRSMapEd, each a real client session
# (struct Editor) with its own EditorView: one stroke, three byte-identical
# mirrors equal to the authority, one undo for the whole stroke; three
# different cameras published under each connection's key, follow, the
# throttle, key ownership, follow selection, a departing connection taking its
# camera with it; the thin toolbar on the real chrome model; SPAWN_VIEW frame
# by frame; the camera text; the launch service's `view` verb under a fake
# spawner; then the DAEMON's SPAWN_VIEW end to end, a script standing in for
# the client. m50_50 is copied under $(OBJ_DIR)/tmp/views; the content tree is
# read only. Links the client's objects minus main.o, as test-edit-world does.
# TORIRSMAPED_VIEWS_TEST_PORT overrides the daemon's port (43619).
.PHONY: test-editor-views
test-editor-views: torirsmaped
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/views_embed_test \
		torirsmaped/test/views_embed_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/views
	mkdir -p $(OBJ_DIR)/tmp/views
	./$(OBJ_DIR)/views_embed_test $(EDITOR_CONTENT_DIR) $(abspath $(OBJ_DIR)/tmp/views) \
		$(abspath $(OBJ_DIR)/torirsmaped) $(abspath ../manifests/manifest_osrs241_mapeditor.ini)

EDITOR_TREE_TESTS += test-editor-views

# The web half: what ?editor_view=1&maped=...&group=N boots as (the same
# command line the launch service starts natively) and the URL "spawn view"
# opens, lifted out of the shipped torirs_host.js. Node only.
.PHONY: test-editor-views-web
test-editor-views-web:
	node web/test/editor_view_test.js

test-editor: test-editor-views-web
