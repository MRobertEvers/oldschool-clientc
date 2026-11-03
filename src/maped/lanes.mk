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

.PHONY: test-lanes
test-lanes:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/lanes_test $(TORIMEM_SRCS) torirsmaped/torirs_maped_lanes.c \
		torirsmaped/test/lanes_test.c
	rm -rf $(OBJ_DIR)/tmp/lanes
	mkdir -p $(OBJ_DIR)/tmp/lanes
	./$(OBJ_DIR)/lanes_test $(abspath $(OBJ_DIR)/tmp/lanes)
test-torirsmaped-daemon: test-lanes
test-editor: test-lanes
