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
# (MAPED_MAP_ARCHIVES: `maps=<(x << 8) | z>` rows, written by the runner), in
# the same cachepack run: a second one loaded the names and opened the cache
# again for one archive.
#
#   make -C src maped-client-cache MAPED_LIVE_CACHE=<lane live dir>/cache \
#       [TORIRSSERVER_CONTENT_DIR=<tree>] [MAPED_MAP_ARCHIVES=<list>]
#
# Refuses a live cache inside the content tree: the tree is a build's input.
MAPED_LIVE_CACHE ?=
MAPED_MAP_ARCHIVES ?=
MAPED_CACHE_REV ?= osrs242
.PHONY: maped-client-cache
maped-client-cache:
	@test -n "$(MAPED_LIVE_CACHE)" || { echo "maped-client-cache: MAPED_LIVE_CACHE is required" >&2; exit 2; }
	@case "$(abspath $(MAPED_LIVE_CACHE))/" in "$(abspath $(TORIRSSERVER_CONTENT_DIR))"/*) \
		echo "maped-client-cache: $(MAPED_LIVE_CACHE) is inside the content tree $(TORIRSSERVER_CONTENT_DIR)" >&2; \
		exit 2;; esac
	$(if $(MAPED_RUN_TOOLS_BUILT),,@$(SUBMAKE) --no-print-directory -C $(RSCACHE_DIR)/tools cachepack > /dev/null)
	cd $(REPO_ROOT) && $(CACHEPACK_BIN) pack --incremental --src $(TORIRSSERVER_CONTENT_DIR) \
		$(if $(wildcard $(TORIRSSERVER_CACHE_BASE)/main_file_cache.dat2),--base $(TORIRSSERVER_CACHE_BASE),) \
		--out $(abspath $(MAPED_LIVE_CACHE)) --rev $(MAPED_CACHE_REV) --defaults=$(CACHEPACK_DEFAULTS) \
		$(if $(MAPED_MAP_ARCHIVES),--assets=maps --archive-list $(abspath $(MAPED_MAP_ARCHIVES)),)

# ---------------------------------------------------------------------------
# RUN's build (torirsmaped/torirs_maped_build.h TORIRSMAPED_BUILD_RUN): the
# packs and the client cache at once. They share no output and no input they
# write, and on a fresh lane each is a full repack of about 20 s, one after the
# other -- RUN's "extremely long time before the client opens" (owner,
# 2026-11-02): the first RUN of the day into build/live/default took 17:52 ->
# 17:53:43 on this Mac. The cachepack tool is built once, serially, before the
# two targets run (both would otherwise rebuild it at the same moment);
# sscompile is torirsserver-packs' own prerequisite.
#
# ONE make: the editor runs `make -j2 maped-run-build`, and the two halves are
# this make's own targets, ordered after maped-run-tools. It was a second
# `$(MAKE) -j2` under this one, and parsing this makefile again was 0.45 s of a
# warm RUN's 2.8 s (2026-11-07). make's error lines still name the half that
# failed ("[torirsserver-packs]", "[maped-client-cache]"): the RUN message
# reads them (torirs_maped_build.c).
#
# A BRAND-NEW lane (no pack.manifest in SERVPACK_OUT / TORIRSSERVER_SCRIPT_OUT /
# TORIRSSERVER_PACKS_LANE_OUT) is seeded from the tree's own pack outputs,
# incremental state included, and the build then brings it up to date -- a copy
# of a few hundred MB instead of a 20 s repack of the server pack and a 3 s
# compile of each script pack. Skipped while the tree's pack is mid-write
# (.pack.lock) and when the tree has none. Nothing is written under the tree.
#
#   make -C src -j2 maped-run-build TORIRSSERVER_CONTENT_DIR=<tree> \
#       TORIRSSERVER_SCRIPT_OUT=<live>/scripts TORIRSSERVER_PACKS_LANE_OUT=<live>/scripts_lanes \
#       SERVPACK_OUT=<live>/servpack MAPED_LIVE_CACHE=<live>/cache [MAPED_MAP_ARCHIVES=<list>] \
#       TORIRSSERVER_CACHE_BASE=<base cache> MAPED_CACHE_REV=osrs242 OBJVERIFY_SKIP=1
.PHONY: maped-lane-seed
maped-lane-seed:
	@seed() { \
		dst="$$1"; src="$$2"; what="$$3"; \
		[ -n "$$dst" ] || return 0; \
		[ ! -e "$$dst/pack.manifest" ] || return 0; \
		[ -e "$$src/pack.manifest" ] || return 0; \
		[ ! -e "$$src/.pack.lock" ] || { echo "maped-lane-seed: $$what: the tree's is being written ($$src/.pack.lock); building it instead"; return 0; }; \
		rm -rf "$$dst"; mkdir -p "$$(dirname "$$dst")"; cp -R "$$src" "$$dst" && rm -f "$$dst/.pack.lock"; \
		echo "maped-lane-seed: $$what seeded from $$src"; \
	}; \
	seed "$(SERVPACK_OUT)" "$(TORIRSSERVER_CONTENT_DIR)/server/pack" "server pack"; \
	seed "$(TORIRSSERVER_SCRIPT_OUT)" "$(TORIRSSERVER_CONTENT_DIR)/server/scripts/build" "script pack (base)"; \
	seed "$(TORIRSSERVER_PACKS_LANE_OUT)" "$(TORIRSSERVER_CONTENT_DIR)/server/scripts/build_summoning_curses" "script pack (lanes)"

# BUILD packs on a lane: the seed, then the tree's own packs target.
.PHONY: maped-lane-packs
maped-lane-packs: maped-lane-seed
	@$(MAKE) --no-print-directory torirsserver-packs

.PHONY: maped-run-build maped-run-tools maped-run-allocate
maped-run-tools: maped-lane-seed
	@$(SUBMAKE) --no-print-directory -C $(RSCACHE_DIR)/tools cachepack > /dev/null

# The ids, once, BEFORE the two halves start. tools/ss_allocate.py is the
# packs half's first step (tools/build_packs.py), and for a var declared with
# no id it does two writes: the ledger line (pack/varp.alloc) and the var's new
# spelling in its block and in every script that reads it. The client cache
# half runs beside it and reads the same tree: caught between the two writes it
# saw `[varp9518_x]` with no line to claim it and stopped the whole RUN
# ("named by neither pack/varp.client nor pack/varp.server ... nothing can
# claim it", 2026-11-11, the first RUN after a quest variable was pasted in).
# Run here, alone, both halves read a tree that is already numbered; the packs
# half's own call then finds its stamp and returns at once.
maped-run-allocate: maped-lane-seed
	@cd $(REPO_ROOT) && python3 tools/ss_allocate.py --tree $(TORIRSSERVER_CONTENT_DIR) \
		--stamp $(TORIRSSERVER_CONTENT_DIR)/server/scripts/build/.ss_allocate.stamp

# Non-empty under `make maped-run-build`: maped-run-tools built cachepack, so
# neither half (maped-client-cache, torirsserver-packs in src/makefile) asks
# its makefile again: 0.12 s on each half of every RUN.
MAPED_RUN_TOOLS_BUILT := $(filter maped-run-build,$(MAKECMDGOALS))
ifneq ($(MAPED_RUN_TOOLS_BUILT),)
torirsserver-packs maped-client-cache: | maped-run-tools maped-run-allocate
endif

maped-run-build: torirsserver-packs maped-client-cache

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
ifeq ($(OS),Windows_NT)
test-live-editor:
	@echo "test-live-editor: SKIPPED on Windows: a POSIX test harness (fork, BSD sockets, /bin/sh fakes for the client and the game server): run it on Linux or macOS; on Windows the paths it covers were run by hand (docs/MAPED_STATUS.md, Windows run)"
else
test-live-editor:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/live_editor_test $(TORIMEM_SRCS) \
		$(filter-out torirsmaped/torirs_maped_main.c,$(TORIRSMAPED_SRCS)) \
		editor/live/live_client.c editor/live/test/live_editor_test.c -lm $(MAPED_TEST_LDFLAGS)
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
endif

EDITOR_TREE_TESTS += test-live-editor

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
		$(addprefix $(OBJ_DIR)/,$(CP_INCREMENTAL_TEST_OBJS)) -lm $(MAPED_TEST_LDFLAGS)
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

EDITOR_TREE_TESTS += test-cp-incremental

# ---------------------------------------------------------------------------
# The Live panel on the real chrome model over a raw connection to a real
# ToriRSMapEd (test runner, native link) and the mock control-link peer:
# hidden at registration, Attach, Follow (subscribe + STATE_LIVE_FOCUS),
# teleport to the selection, the cheat line, RUN, Build packs, a focus from
# another window, Detach. Links the client's objects minus main.o, as
# test-editor-panels does. Writes only under $(OBJ_DIR)/tmp/live_panel.
.PHONY: test-live-panel
ifeq ($(OS),Windows_NT)
test-live-panel:
	@echo "test-live-panel: SKIPPED on Windows: a POSIX test harness (fork, BSD sockets, /bin/sh fakes for the client and the game server): run it on Linux or macOS; on Windows the paths it covers were run by hand (docs/MAPED_STATUS.md, Windows run)"
else
test-live-panel:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/live_panel_test editor/live/test/live_panel_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS) $(MAPED_TEST_LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/live_panel
	mkdir -p $(OBJ_DIR)/tmp/live_panel/content/maps
	cp $(EDITOR_CONTENT_DIR)/maps/m50_50.jm2 $(EDITOR_CONTENT_DIR)/maps/m50_50.jl2 \
		$(OBJ_DIR)/tmp/live_panel/content/maps/
	cp editor/live/test/mock_control_peer.py $(OBJ_DIR)/tmp/live_panel/
	./$(OBJ_DIR)/live_panel_test $(abspath $(OBJ_DIR)/tmp/live_panel) \
		$(abspath $(OBJ_DIR)/tmp/live_panel/mock_control_peer.py) $(REPO_ROOT)
endif

EDITOR_TREE_TESTS += test-live-panel
