# Editor lanes (torirsmaped/torirs_maped_lanes.h): the working lane, publish.
# Included from src/makefile beside palette.mk and accounts.mk.
#
# torirs_maped_lanes.c is a plain unit (the registry, the mirror, changes,
# publish, discard: no wire, no server state) in TORIRSMAPED_SRCS and in the
# embedded editor server's client sources; the LANE_* request handlers are
# torirs_maped_lanes_wire.c, compiled INTO torirs_maped.c like the accounts
# half (never on a link line of its own).

# Compiled INTO torirs_maped.c (like the accounts half), so every link line
# that names torirs_maped.c carries it; the standalone test compiles it alone.
# The Publish panel, in the client (edpanels.h).
MAPED_CLIENT_SRCS += editor/panels/edpanel_publish.c

# Both tests build WITH asserts (OPT=1 puts -DNDEBUG in CFLAGS): the lanes
# code's contract asserts are what they test as much as its answers.
LANES_TEST_CFLAGS = $(filter-out -DNDEBUG,$(CFLAGS)) -UNDEBUG

.PHONY: test-lanes
test-lanes:
	mkdir -p $(OBJ_DIR)
	$(CC) $(LANES_TEST_CFLAGS) -o $(OBJ_DIR)/lanes_test $(TORIMEM_SRCS) torirsmaped/torirs_maped_lanes.c \
		torirsmaped/test/lanes_test.c $(MAPED_TEST_LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/lanes
	mkdir -p $(OBJ_DIR)/tmp/lanes
	./$(OBJ_DIR)/lanes_test $(abspath $(OBJ_DIR)/tmp/lanes)
test-torirsmaped-daemon: test-lanes
test-editor: test-lanes

# The editor server's own saves on a sparse lane: the real local host with
# the lane's write guard (Editor_HostLocalSetWriteGuard), nothing written
# through a link into the base, a refused or missed guard failing the save.
# Twice: with asserts (every contract checked), and as the client ships
# (-DNDEBUG), where a missed guard must still be refused, not abort.
LANES_HOST_TEST_SRCS = $(TORIMEM_SRCS) torirsmaped/torirs_maped_lanes.c \
	editor/editor_jm2.c editor/editor_doc.c editor/editor_cmd.c editor/editor_spawn.c \
	editor/editor_cfg.c editor/editor_if.c editor/editor_host_local.c torirsmaped/test/lanes_host_test.c

.PHONY: test-lanes-host
test-lanes-host:
	mkdir -p $(OBJ_DIR)
	$(CC) $(LANES_TEST_CFLAGS) -o $(OBJ_DIR)/lanes_host_test $(LANES_HOST_TEST_SRCS) $(MAPED_TEST_LDFLAGS)
	$(CC) $(filter-out -UNDEBUG,$(CFLAGS)) -DNDEBUG -o $(OBJ_DIR)/lanes_host_test_ndebug $(LANES_HOST_TEST_SRCS) \
		$(MAPED_TEST_LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/lanes_host $(OBJ_DIR)/tmp/lanes_host_ndebug
	mkdir -p $(OBJ_DIR)/tmp/lanes_host $(OBJ_DIR)/tmp/lanes_host_ndebug
	./$(OBJ_DIR)/lanes_host_test $(abspath $(OBJ_DIR)/tmp/lanes_host)
	./$(OBJ_DIR)/lanes_host_test_ndebug $(abspath $(OBJ_DIR)/tmp/lanes_host_ndebug)
test-torirsmaped-daemon: test-lanes-host
test-editor: test-lanes-host
