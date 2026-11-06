# Accounts (docs/MAPED_STATUS.md "Accounts"): the lane's player saves managed
# from the editor -- the daemon module torirsmaped/torirs_maped_accounts.c
# (compiled INTO torirsmaped/torirs_maped.c after the live half, so no link
# line names it), the client model editor/accounts/accounts_client.c, the
# Accounts panel editor/accounts/accounts_panel.c, and the Place player tool.
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS and the content paths are defined: rule lines here name no
# such variable (it would expand empty), recipes use them freely.

# The client model and the panel link into every client build (OBJS), as the
# Live panel's do.
MAPED_CLIENT_SRCS += editor/accounts/accounts_client.c editor/accounts/accounts_panel.c

# The daemon module's pure pieces (the save codec, the quest arms parser, the
# stat table) and its requests against a real ToriRSMapEd over a scratch
# content tree with a lane beside it -- list, create, every offline SET key,
# the quest queue, use, duplicate, delete -- then the ONLINE path against the
# python mock control-link peer (editor/live/test/mock_control_peer.py): a
# SET becomes a cheat the peer logs plus a checkpoint request, the queue
# flushes after the login, a checkpoint file becomes the view's snapshot.
# Writes only under $(OBJ_DIR)/tmp/accounts; the tree is not read.
.PHONY: test-accounts
test-accounts:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/accounts_test $(TORIMEM_SRCS) \
		$(filter-out torirsmaped/torirs_maped_main.c,$(TORIRSMAPED_SRCS)) \
		torirsmaped/test/accounts_test.c -lm
	rm -rf $(OBJ_DIR)/tmp/accounts
	mkdir -p $(OBJ_DIR)/tmp/accounts
	cp editor/live/test/mock_control_peer.py $(OBJ_DIR)/tmp/accounts/
	./$(OBJ_DIR)/accounts_test $(abspath $(OBJ_DIR)/tmp/accounts) \
		$(abspath $(OBJ_DIR)/tmp/accounts/mock_control_peer.py) $(REPO_ROOT)

test-torirsmaped-daemon: test-accounts
test-editor: test-accounts

# Accounts against a REAL lane (the real torirsserver, js5_server and play
# client, as maped-live-e2e-play): an account made and edited offline, logged
# in as the play account, edited online (cheats + the server's checkpoint),
# a queued quest cheat flushed at its next login. Writes only under
# $(OBJ_DIR)/tmp/live_accounts; OSRS-Content is read through a mirror.
# LIVE_E2E_SERVER_OBJ names the objdir holding torirsserver/js5_server
# (default build_opt); LIVE_E2E_PLAY_BIN the client (default src/torirs).
.PHONY: maped-live-e2e-accounts
maped-live-e2e-accounts: torirsmaped torirsmapedctl
	LIVE_E2E_OBJ=$(OBJ_DIR) sh maped/test/live_e2e/run_accounts.sh

# The panel, headless, against the real chrome model and an embedded
# ToriRSMapEd with a lane over a fixture tree + live dir the test writes under
# $(OBJ_DIR)/tmp/editor_panels_accounts: the list, a stat edit writing the
# save, Move and the Place player tool, Find + Set through the catalog, the
# Quests rows and a queued mark, the Items slots (by name, from the Catalog
# pick, removed), New account / Use / Delete, a second window following the
# list. Links the client's objects minus main.o, as test-editor-panels does.
.PHONY: test-editor-panels-accounts
test-editor-panels-accounts:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_panels_accounts_test editor/test/editor_panels_accounts_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/editor_panels_accounts
	mkdir -p $(OBJ_DIR)/tmp
	./$(OBJ_DIR)/editor_panels_accounts_test $(abspath $(OBJ_DIR)/tmp/editor_panels_accounts) $(REPO_ROOT)

test-editor: test-editor-panels-accounts
