# Phase 4, owner `live-server` (docs/MAPED_IMPLEMENTATION.md section 1).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS and the server source lists are defined: rule lines here
# name no such variable (it would expand empty), recipes use them freely and
# build their prerequisites through a sub-make.

# The live reload against the real server, the real content (read only) and
# two script packs the test compiles itself under $(OBJ_DIR)/tmp/live_reload
# (editor/builder/builder_ssc.c stages what sscompile reads; nothing is written
# under the content tree):
#   - a player parked in p_pausebutton across a script reload finishes the old
#     dialogue and gets the new one on the next click;
#   - a square reload sends exactly one REBUILD to the covering player;
#   - the edited lane's spawns stand up and go; the generated lane is refused;
#   - the control link through the game port's first-byte select (0xED),
#     HELLO / STATUS / RELOAD / CHEAT / SUBSCRIBE / BYE, and the gate's ticket.
# Links the server core the way test-torirsserver-embed does.
LIVE_SERVER_CACHE_DIR ?= $(REPO_ROOT)/cache.osrs241
.PHONY: test-live-reload
test-live-reload:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(SSC_BIN) $(NET_CORE_OBJS) $(OBJ_DIR)/net.o $(OBJ_DIR)/cmdring.o
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/live_reload_test \
		torirsserver/test/live_reload_test.c editor/builder/builder_ssc.c \
		platform/launch_sessions.c \
		$(filter-out net/proto/proto_info.c torirsserver/wire/osrs241_clientprot.c,$(TORIRSSERVER_CORE_SRCS)) \
		net/net_out.c \
		$(NET_CORE_OBJS) $(OBJ_DIR)/net.o $(OBJ_DIR)/cmdring.o -lm $(MAPED_TEST_LDFLAGS)
	mkdir -p $(OBJ_DIR)/tmp/live_reload
	./$(OBJ_DIR)/live_reload_test $(EDITOR_CONTENT_DIR) $(LIVE_SERVER_CACHE_DIR) \
		$(abspath $(SSC_BIN)) $(abspath $(OBJ_DIR)/tmp/live_reload)
