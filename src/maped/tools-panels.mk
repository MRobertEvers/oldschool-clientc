# Phase 3, owner `tools-panels` (docs/MAPED_IMPLEMENTATION.md section 1).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS, OBJS and the content paths are defined: rule lines here name
# no such variable (it would expand empty), recipes use them freely.

# The panel registry and its modules link into every client build (src/makefile
# appends MAPED_CLIENT_SRCS to SRCS). Object names are basenames, so every
# module is edpanel_<name>.c.
TOOLS_PANELS_SRCS = editor/editor_ui.c editor/editor_path.c editor/editor_region.c \
	editor/panels/edpanel_common.c editor/panels/edpanel_actions.c \
	editor/panels/edpanel_ops.c editor/panels/edpanel_picker.c \
	editor/panels/edpanel_newform.c editor/panels/edpanel_shapes.c \
	editor/panels/edpanel_console.c editor/panels/edpanel_saveguard.c \
	editor/panels/edpanel_menubar.c editor/panels/edpanel_toolrail.c \
	editor/panels/edpanel_terrain.c editor/panels/edpanel_palette.c \
	editor/panels/edpanel_rect.c editor/panels/edpanel_squares.c \
	editor/panels/edpanel_newsquare.c editor/panels/edpanel_locs.c \
	editor/panels/edpanel_spawns.c editor/panels/edpanel_catalog.c \
	editor/panels/edpanel_catalog_model.c editor/panels/edpanel_preview.c editor/panels/edpanel_gallery.c \
	editor/panels/edpanel_inspector.c editor/panels/edpanel_record.c
# The History panel (editing feedback): the undo groups, click to jump.
TOOLS_PANELS_SRCS += editor/panels/edpanel_history.c
# The status bar along the foot (design #workbench): the hint strip, the
# hover readout, the status sentence.
TOOLS_PANELS_SRCS += editor/panels/edpanel_statusbar.c
# The floor colours as the client blends and draws them (the Palette's
# picker, blend strip and swatch readouts), headless.
TOOLS_PANELS_SRCS += editor/editor_floorcolor.c
MAPED_CLIENT_SRCS += $(TOOLS_PANELS_SRCS)

# The panels, headless, against the real chrome model and an embedded
# ToriRSMapEd over a small content tree copied under $(OBJ_DIR)/tmp/editor_panels
# (two maps/ squares and their neighbours, a dungeon square, an instance one, a lane holding one of them, an empty
# lane, the world-map areas, the floor configs, one generated spawn file):
#
#   ops        brushes (raise/lower/flatten/smooth/paint, falloff, the seam),
#              the rectangle (copy/paste/fill/clear), loc multi-select ops
#              (nudge across a square border, rotate, reshape, duplicate,
#              delete) and spawn place/delete/suppress/restore, each with undo
#   picker     the atlas cut into area tabs, search (m50_50 / 50 50 / tile /
#              area), the cell grid's hit test, zoom about the pointer, pan,
#              the GRAPH picture and its keys
#   New        form checks: taken in that storage (owner named), lanes, new
#              lane names, the copy modes, the flat fill
#   chrome     the registered modules driven by GRAPH events and activations:
#              click opens, shift-click adds, right-click menu, Copy to, the
#              New modal's DOC_CREATE into a lane, the refusal, the save guard,
#              the tool rail, the console
#   shapes     the overlay thumbnails sampled from world_decode_tile
#
# Links the client's objects minus main.o, as test-edit-world does. The content
# tree and the cache are READ ONLY; everything written lands in the tmp copy.
.PHONY: test-editor-panels
test-editor-panels:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_panels_test editor/test/editor_panels_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS) $(MAPED_TEST_LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/editor_panels
	mkdir -p $(OBJ_DIR)/tmp/editor_panels/maps $(OBJ_DIR)/tmp/editor_panels/configs \
		$(OBJ_DIR)/tmp/editor_panels/worldmap \
		$(OBJ_DIR)/tmp/editor_panels/ported/testlane/maps \
		$(OBJ_DIR)/tmp/editor_panels/ported/emptylane/maps \
		$(OBJ_DIR)/tmp/editor_panels/server/scripts/areas/world/configs \
		$(OBJ_DIR)/tmp/editor_panels/server/scripts/areas/edited/configs
	for sq in m50_50 m50_51 m51_50 m27_83 m51_196; do \
		cp $(EDITOR_CONTENT_DIR)/maps/$$sq.jm2 $(EDITOR_CONTENT_DIR)/maps/$$sq.jl2 \
			$(OBJ_DIR)/tmp/editor_panels/maps/ ; done
	cp $(EDITOR_CONTENT_DIR)/maps/m50_50.jm2 $(EDITOR_CONTENT_DIR)/maps/m50_50.jl2 \
		$(OBJ_DIR)/tmp/editor_panels/ported/testlane/maps/
	cp $(EDITOR_CONTENT_DIR)/configs/all.underlay $(EDITOR_CONTENT_DIR)/configs/all.overlay \
		$(EDITOR_CONTENT_DIR)/configs/all.underlay.compack \
		$(EDITOR_CONTENT_DIR)/configs/all.overlay.compack $(OBJ_DIR)/tmp/editor_panels/configs/
	cp -R $(EDITOR_CONTENT_DIR)/worldmap/areas $(OBJ_DIR)/tmp/editor_panels/worldmap/
	cp $(EDITOR_CONTENT_DIR)/server/scripts/areas/world/configs/m50_50.spawn \
		$(OBJ_DIR)/tmp/editor_panels/server/scripts/areas/world/configs/
	./$(OBJ_DIR)/editor_panels_test $(OBJ_DIR)/tmp/editor_panels

EDITOR_TREE_TESTS += test-editor-panels
