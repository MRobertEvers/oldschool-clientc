# Workbench 6.7, part 2 (docs/MAPED_STATUS.md "Workbench step 6.7, part 2"):
# the shop builder (editor/editor_shop.c, the Shop panel
# editor/panels/edpanel_shop.c) and the quest board (editor/editor_quest.c,
# the Quest board panel editor/panels/edpanel_quest.c).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS, OBJS and the content paths are defined: rule lines here name
# no such variable (it would expand empty), recipes use them freely.

MAPED_CLIENT_SRCS += editor/editor_shop.c editor/editor_quest.c \
	editor/panels/edpanel_shop.c editor/panels/edpanel_quest.c

# The shop model on its own, content-free: tools/gen_shop_scripts.py's own
# renderers write the samples' pairs (editor/test/editor_shop_oracle.py, the
# rows in editor/test/shop_samples.tsv), and editor_shop.c must emit the same
# bytes for the same rows; the pairs parse back and re-emit; calc_shop_value
# by hand; the model's refusals; the wiki stock catalogue's rows.
.PHONY: test-editor-shop
test-editor-shop:
	mkdir -p $(OBJ_DIR)
	rm -rf $(OBJ_DIR)/tmp/editor_shop_oracle
	python3 -I editor/test/editor_shop_oracle.py $(REPO_ROOT)/tools editor/test/shop_samples.tsv \
		$(OBJ_DIR)/tmp/editor_shop_oracle
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_shop_test $(TORIMEM_SRCS) editor/editor_shop.c \
		editor/test/editor_shop_test.c
	./$(OBJ_DIR)/editor_shop_test editor/test/shop_samples.tsv $(OBJ_DIR)/tmp/editor_shop_oracle

test-editor: test-editor-shop

# The quest board's reader over a SYNTHETIC repository and content tree the
# test writes under $(OBJ_DIR)/tmp/editor_quest (QUEUE.tsv, the inventory, a
# quest's .constant and scripts, an npc builder model, a test file, a ledger
# and a gate verdict): every row of design section 11's figure and its state
# word, the ledger's rows and SUMMARY, the shot stems, the verdict going
# stale when the ledger is newer, and the Scaffold / Run gate refusals.
.PHONY: test-editor-quest
test-editor-quest:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_quest_test $(TORIMEM_SRCS) editor/editor_quest.c \
		editor/test/editor_quest_test.c
	rm -rf $(OBJ_DIR)/tmp/editor_quest
	mkdir -p $(OBJ_DIR)/tmp
	./$(OBJ_DIR)/editor_quest_test $(OBJ_DIR)/tmp/editor_quest

test-editor: test-editor-quest

# The two panels, headless, against the real chrome model and an embedded
# ToriRSMapEd whose catalog indexes a SYNTHETIC content tree and whose BUILD
# runner is the test runner (no make, no python, no OSRS-Content): a shop
# typed in, priced, checked, saved as one undo step byte for byte, undone and
# redone; Check's catalog refusals; the generator's pair read-only; the wiki
# rows; Test refused then RUN sent; the keycaps; the quest board's rows, Open
# part, Run gate's refusal and its make line, its verdict read back; Scaffold's
# refusal; the ledger and its screenshot (png_publish, or the path); the two
# View actions. Links the client's objects minus main.o, as
# test-editor-stamps-ops does.
.PHONY: test-editor-shop-quest-panels
test-editor-shop-quest-panels:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_shop_quest_panels_test editor/test/editor_shop_quest_panels_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/editor_shop_quest
	mkdir -p $(OBJ_DIR)/tmp/editor_shop_quest
	./$(OBJ_DIR)/editor_shop_quest_panels_test $(abspath $(OBJ_DIR)/tmp/editor_shop_quest)

test-editor: test-editor-shop-quest-panels

.PHONY: test-editor-shop-quest
test-editor-shop-quest: test-editor-shop test-editor-quest test-editor-shop-quest-panels

# ---------------------------------------------------------------------------
# The quest board's BUILD jobs (torirsmaped/torirs_maped_build.h
# TORIRSMAPED_BUILD_QUEST_SCAFFOLD / _GATE): the quest gate's own tools, run
# by the editor server's BuildRunner so nothing of them runs on the editor's
# UI thread; every line they print streams to the Live panel and the board.
#
#   make -C src maped-quest-scaffold MAPED_QUEST=<test_id>
#       tools/quest_gate/new_quest.py <test_id> (which refuses to clobber a
#       test file); MAPED_QUEST_HELPER_ROOT (default the environment's
#       TORIRS_QUEST_HELPER_ROOT) is its --qh-root
#   make -C src maped-quest-gate MAPED_QUEST=<test_id>
#       run.py <test_id> --no-build, then gate.py <test_id>, whose output goes
#       to build/maped_quest_board/<test_id>.gate under a header naming this
#       target -- the directory is this target's; the Quest board reads the
#       verdict there and calls it stale once the ledger is newer. run.py's
#       exit is not the verdict (docs/QUEST_AUTHORING.md section 6), so the
#       job fails only when gate.py printed no verdict for the quest.
MAPED_QUEST ?=
MAPED_QUEST_HELPER_ROOT ?= $(TORIRS_QUEST_HELPER_ROOT)
MAPED_QUEST_BOARD_DIR = $(REPO_ROOT)/build/maped_quest_board
.PHONY: maped-quest-scaffold maped-quest-gate
maped-quest-scaffold:
	@case "$(MAPED_QUEST)" in ""|*[!a-z0-9_]*) \
		echo "maped-quest-scaffold: MAPED_QUEST=<test_id> (a-z, 0-9, _) is required" >&2; exit 2;; esac
	cd $(REPO_ROOT) && python3 tools/quest_gate/new_quest.py $(MAPED_QUEST) \
		$(if $(MAPED_QUEST_HELPER_ROOT),--qh-root $(MAPED_QUEST_HELPER_ROOT),)

maped-quest-gate:
	@case "$(MAPED_QUEST)" in ""|*[!a-z0-9_]*) \
		echo "maped-quest-gate: MAPED_QUEST=<test_id> (a-z, 0-9, _) is required" >&2; exit 2;; esac
	cd $(REPO_ROOT) && python3 tools/quest_gate/run.py $(MAPED_QUEST) --no-build \
		|| echo "maped-quest-gate: run.py exited $$?; gate.py grades what it left"
	@mkdir -p $(MAPED_QUEST_BOARD_DIR)
	@cd $(REPO_ROOT) && { \
		echo "# Written by make maped-quest-gate (src/maped/shop-quest.mk): tools/quest_gate/gate.py $(MAPED_QUEST), replaced every run."; \
		python3 tools/quest_gate/gate.py $(MAPED_QUEST); echo "# gate.py exited $$?"; \
		} > $(MAPED_QUEST_BOARD_DIR)/$(MAPED_QUEST).gate.tmp
	@mv $(MAPED_QUEST_BOARD_DIR)/$(MAPED_QUEST).gate.tmp $(MAPED_QUEST_BOARD_DIR)/$(MAPED_QUEST).gate
	@cat $(MAPED_QUEST_BOARD_DIR)/$(MAPED_QUEST).gate
	@grep -Eq "^$(MAPED_QUEST) +(green|RED|blocked)" $(MAPED_QUEST_BOARD_DIR)/$(MAPED_QUEST).gate \
		|| { echo "maped-quest-gate: gate.py printed no verdict for $(MAPED_QUEST)" >&2; exit 1; }
