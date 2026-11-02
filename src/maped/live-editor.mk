# Phase 4, owner `live-editor` (docs/MAPED_IMPLEMENTATION.md section 1).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS, OBJS and the content paths are defined: rule lines here name
# no such variable (it would expand empty), recipes use them freely.
#
# No source hook for the server half: torirsmaped/torirs_maped_build.c and
# torirs_maped_live.c are compiled INTO torirsmaped/torirs_maped.c (#includes
# at its end, like the catalog and the views), so every link line that names
# torirs_maped.c carries BUILD, RUN and the live link. Never add them to a link.

# The Live panel's model and its chrome panel, in the client.
LIVE_PANEL_SRCS = editor/live/live_client.c editor/live/live_panel.c
MAPED_CLIENT_SRCS += $(LIVE_PANEL_SRCS)
# The panel over the editor host (Editor_Live_Register), through
# Editor_HostMapEdLiveSend / Editor_HostMapEdLiveTake (editor_host_remote.c).
LIVE_HOST_SRCS = editor/live/live_host.c
MAPED_CLIENT_SRCS += $(LIVE_HOST_SRCS)

# ---------------------------------------------------------------------------
# The client cache half of a live build: what the native BuildRunner runs for
# BUILD client cache (torirsmaped/torirs_maped_build.h). The incremental config
# pack writes IN PLACE into MAPED_LIVE_CACHE (copied from the base the first
# time; cp_incremental.h), then the squares the editor saved go in by archive
# (MAPED_MAP_ARCHIVES: `maps=<(x << 8) | z>` rows, written by the runner).
#
#   make -C src maped-client-cache MAPED_LIVE_CACHE=<lane live dir>/cache \
#       [TORIRSSERVER_CONTENT_DIR=<tree>] [MAPED_MAP_ARCHIVES=<list>]
#
# Refuses a live cache inside the content tree: the tree is a build's input.
MAPED_LIVE_CACHE ?=
MAPED_MAP_ARCHIVES ?=
MAPED_CACHE_REV ?= osrs241
.PHONY: maped-client-cache
maped-client-cache:
	@test -n "$(MAPED_LIVE_CACHE)" || { echo "maped-client-cache: MAPED_LIVE_CACHE is required" >&2; exit 2; }
	@case "$(abspath $(MAPED_LIVE_CACHE))/" in "$(abspath $(TORIRSSERVER_CONTENT_DIR))"/*) \
		echo "maped-client-cache: $(MAPED_LIVE_CACHE) is inside the content tree $(TORIRSSERVER_CONTENT_DIR)" >&2; \
		exit 2;; esac
	@$(SUBMAKE) --no-print-directory -C $(RSCACHE_DIR)/tools cachepack > /dev/null
	cd $(REPO_ROOT) && $(CACHEPACK_BIN) pack --incremental --src $(TORIRSSERVER_CONTENT_DIR) \
		$(if $(wildcard $(TORIRSSERVER_CACHE_BASE)/main_file_cache.dat2),--base $(TORIRSSERVER_CACHE_BASE),) \
		--out $(abspath $(MAPED_LIVE_CACHE)) --rev $(MAPED_CACHE_REV) --defaults=$(CACHEPACK_DEFAULTS)
	$(if $(MAPED_MAP_ARCHIVES),cd $(REPO_ROOT) && $(CACHEPACK_BIN) pack --asset-only --assets=maps \
		--archive-list $(abspath $(MAPED_MAP_ARCHIVES)) --src $(TORIRSSERVER_CONTENT_DIR) \
		--out $(abspath $(MAPED_LIVE_CACHE)) --rev $(MAPED_CACHE_REV),@true)

# ---------------------------------------------------------------------------
# BUILD, RUN and the live link against a real ToriRSMapEd (no chrome): the
# test BuildRunner records the make lines and answers from a script; a python
# MOCK CONTROL-LINK PEER (editor/live/test/mock_control_peer.py, brief section
# 4 over real TCP) stands in for the game server, attached by address and then
# LAUNCHED as the lane's torirsserver beside a fake js5_server. Covers RUN's
# sequencing (save -> packs -> client cache -> reload -> RUN_DONE ->
# LIVE_STATUS) and its stop at the first failure, cheat, subscribe, the gate
# ticket, detach and the launched processes' lifetime, plus the client model,
# and the play client: the native launcher on a fake client binary
# (editor/live/test/fake_play.sh) and RUN's play step against the peer's
# player list (start, reloaded-in-place, logged out, died, Play/Stop, browser).
# One square copied under $(OBJ_DIR)/tmp/live_editor; the tree is read only.
.PHONY: test-live-editor
test-live-editor:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/live_editor_test $(TORIMEM_SRCS) \
		$(filter-out torirsmaped/torirs_maped_main.c,$(TORIRSMAPED_SRCS)) \
		editor/live/live_client.c editor/live/test/live_editor_test.c
	rm -rf $(OBJ_DIR)/tmp/live_editor
	mkdir -p $(OBJ_DIR)/tmp/live_editor/content/maps \
		$(OBJ_DIR)/tmp/live_editor/content/server/scripts/areas/edited/configs
	cp $(EDITOR_CONTENT_DIR)/maps/m50_50.jm2 $(EDITOR_CONTENT_DIR)/maps/m50_50.jl2 \
		$(OBJ_DIR)/tmp/live_editor/content/maps/
	cp editor/live/test/mock_control_peer.py editor/live/test/fake_server.sh editor/live/test/fake_play.sh \
		$(OBJ_DIR)/tmp/live_editor/
	chmod +x $(OBJ_DIR)/tmp/live_editor/mock_control_peer.py $(OBJ_DIR)/tmp/live_editor/fake_server.sh \
		$(OBJ_DIR)/tmp/live_editor/fake_play.sh
	./$(OBJ_DIR)/live_editor_test $(abspath $(OBJ_DIR)/tmp/live_editor) \
		$(abspath $(OBJ_DIR)/tmp/live_editor/mock_control_peer.py) \
		$(abspath $(OBJ_DIR)/tmp/live_editor/fake_server.sh) $(REPO_ROOT) \
		$(abspath $(OBJ_DIR)/tmp/live_editor/fake_play.sh)

test-editor: test-live-editor

# ---------------------------------------------------------------------------
# The incremental client pack (cp_incremental.h) on a miniature tree copied
# under $(OBJ_DIR)/tmp/cp_incremental: meta.ini, content.ini, fields/, pack/,
# configs/all.{underlay,overlay,varp} and the raw config groups. Only the
# changed type repacks, only its archive is written, the reference table is
# bumped, the result equals a full pack's, a comment edit writes nothing, and
# maped-client-cache drives it (and refuses a cache inside the tree).
CP_INCREMENTAL_TEST_OBJS = rscache_unity.o bzip.o bzip_encode.o miniz.o xteas.o
.PHONY: test-cp-incremental
test-cp-incremental:
	mkdir -p $(OBJ_DIR)
	$(SUBMAKE) --no-print-directory -C $(RSCACHE_DIR)/tools cachepack > /dev/null
	$(MAKE) $(addprefix $(OBJ_DIR)/,$(CP_INCREMENTAL_TEST_OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/cp_incremental_test $(TORIMEM_SRCS) \
		editor/live/test/cp_incremental_test.c \
		$(addprefix $(OBJ_DIR)/,$(CP_INCREMENTAL_TEST_OBJS)) -lm
	rm -rf $(OBJ_DIR)/tmp/cp_incremental
	mkdir -p $(OBJ_DIR)/tmp/cp_incremental/src/configs
	cp $(EDITOR_CONTENT_DIR)/meta.ini $(EDITOR_CONTENT_DIR)/content.ini $(OBJ_DIR)/tmp/cp_incremental/src/
	cp -R $(EDITOR_CONTENT_DIR)/fields $(EDITOR_CONTENT_DIR)/pack $(OBJ_DIR)/tmp/cp_incremental/src/
	cp $(EDITOR_CONTENT_DIR)/configs/all.underlay $(EDITOR_CONTENT_DIR)/configs/all.underlay.compack \
		$(EDITOR_CONTENT_DIR)/configs/all.overlay $(EDITOR_CONTENT_DIR)/configs/all.overlay.compack \
		$(EDITOR_CONTENT_DIR)/configs/all.varp $(EDITOR_CONTENT_DIR)/configs/all.varp.compack \
		$(EDITOR_CONTENT_DIR)/configs/config_* $(OBJ_DIR)/tmp/cp_incremental/src/configs/
	./$(OBJ_DIR)/cp_incremental_test $(abspath $(OBJ_DIR)/tmp/cp_incremental) $(CACHEPACK_BIN) \
		$(MAKE) $(SRC_DIR)

test-editor: test-cp-incremental

# ---------------------------------------------------------------------------
# The Live panel on the real chrome model over a raw connection to a real
# ToriRSMapEd (test runner, native link) and the mock control-link peer:
# hidden at registration, Attach, Follow (subscribe + STATE_LIVE_FOCUS),
# teleport to the selection, the cheat line, RUN, Build packs, a focus from
# another window, Detach. Links the client's objects minus main.o, as
# test-editor-panels does. Writes only under $(OBJ_DIR)/tmp/live_panel.
.PHONY: test-live-panel
test-live-panel:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/live_panel_test editor/live/test/live_panel_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/live_panel
	mkdir -p $(OBJ_DIR)/tmp/live_panel/content/maps
	cp $(EDITOR_CONTENT_DIR)/maps/m50_50.jm2 $(EDITOR_CONTENT_DIR)/maps/m50_50.jl2 \
		$(OBJ_DIR)/tmp/live_panel/content/maps/
	cp editor/live/test/mock_control_peer.py $(OBJ_DIR)/tmp/live_panel/
	./$(OBJ_DIR)/live_panel_test $(abspath $(OBJ_DIR)/tmp/live_panel) \
		$(abspath $(OBJ_DIR)/tmp/live_panel/mock_control_peer.py) $(REPO_ROOT)

test-editor: test-live-panel
