# Workbench 6.6, part 1 (docs/MAPED_STATUS.md "Workbench step 6.6, part 1"):
# the Record panel's typed rows, tabs, search and Where used
# (editor/panels/edpanel_record.c over editor/editor_cfg.c's register types),
# the Problems panel (editor/panels/edpanel_problems.c), and Check and CS2
# compile as the editor server's BUILD jobs (torirsmaped/torirs_maped_build.h
# TORIRSMAPED_BUILD_CHECK / _CS2).
#
# Included from src/makefile right after platform_check.mk, i.e. BEFORE
# OBJ_DIR, CFLAGS, OBJS and the content paths are defined: rule lines here name
# no such variable (it would expand empty), recipes use them freely.

MAPED_CLIENT_SRCS += editor/panels/edpanel_problems.c

# ---------------------------------------------------------------------------
# The two BUILD jobs. The editor server's BuildRunner runs them (never an
# editor's UI thread); every line they print streams as FACT_BUILD_LINE to
# every window's Live panel and Problems panel.
#
#   make -C src maped-script-check TORIRSSERVER_CONTENT_DIR=<tree> MAPED_CHECK_DIR=<live>/check
#       maped_script_check (editor/builder/builder_ssc_main.c): the builders'
#       stage of server/scripts under MAPED_CHECK_DIR/scripts, the server's
#       unsaved script documents (MAPED_CHECK_DIR/puts) over it, sscompile,
#       one `<file>:<line>: [warning: ]<message>` line per diagnostic and a
#       `check: ...` summary. Nothing is written in the content tree (the
#       tool refuses a MAPED_CHECK_DIR inside it).
#   make -C src maped-cs2-compile MAPED_CS2_DIR=<live>/cs2 MAPED_CS2_SCRIPT=<name> \
#       MAPED_CS2_CACHE=<cache> MAPED_CACHE_REV=osrs241
#       `cs2 compile` of the one script the server wrote to
#       MAPED_CS2_DIR/src/<name>.cs2, into MAPED_CS2_DIR/out; --names is
#       RuneStar's tables (RUNESTAR_CS2_NAMES) when they are on this machine.
MAPED_CHECK_DIR ?=
MAPED_CS2_DIR ?=
MAPED_CS2_SCRIPT ?=
MAPED_CS2_CACHE ?=
.PHONY: maped-script-check maped-cs2-compile
maped-script-check: sscompile
	@[ -n "$(MAPED_CHECK_DIR)" ] || { echo "maped-script-check: MAPED_CHECK_DIR=<dir> is required" >&2; exit 2; }
	@mkdir -p $(OBJ_DIR) $(MAPED_CHECK_DIR)
	@[ $(OBJ_DIR)/maped_script_check -nt editor/builder/builder_ssc.c ] \
		&& [ $(OBJ_DIR)/maped_script_check -nt editor/builder/builder_ssc_main.c ] \
		|| $(CC) $(CFLAGS) -o $(OBJ_DIR)/maped_script_check $(TORIMEM_SRCS) editor/builder/builder_ssc.c \
			editor/builder/builder_ssc_main.c
	@$(abspath $(OBJ_DIR))/maped_script_check $(abspath $(SSC_BIN)) $(TORIRSSERVER_CONTENT_DIR) \
		$(abspath $(MAPED_CHECK_DIR))

maped-cs2-compile:
	@[ -n "$(MAPED_CS2_DIR)" ] || { echo "maped-cs2-compile: MAPED_CS2_DIR=<dir> is required" >&2; exit 2; }
	@case "$(MAPED_CS2_SCRIPT)" in ""|-*|.*|*[!A-Za-z0-9_.-]*) \
		echo "maped-cs2-compile: MAPED_CS2_SCRIPT=<name> ([A-Za-z0-9_.-]) is required" >&2; exit 2;; esac
	@[ -f "$(MAPED_CS2_DIR)/src/$(MAPED_CS2_SCRIPT).cs2" ] || { \
		echo "error: maped-cs2-compile: $(MAPED_CS2_DIR)/src/$(MAPED_CS2_SCRIPT).cs2 was not written" >&2; exit 2; }
	@$(SUBMAKE) --no-print-directory -C $(RSCACHE_DIR)/tools cs2 > /dev/null
	@mkdir -p $(MAPED_CS2_DIR)/out
	@echo "cs2: compiling $(MAPED_CS2_SCRIPT).cs2"
	$(CS2_BIN) compile --src $(MAPED_CS2_DIR)/src \
		--cache $(if $(MAPED_CS2_CACHE),$(MAPED_CS2_CACHE),$(REPO_ROOT)/cache.$(if $(MAPED_CACHE_REV),$(MAPED_CACHE_REV),osrs241)) \
		--rev $(if $(MAPED_CACHE_REV),$(MAPED_CACHE_REV),osrs241) \
		$(if $(wildcard $(RUNESTAR_CS2_NAMES)),--names $(RUNESTAR_CS2_NAMES),) \
		--out $(MAPED_CS2_DIR)/out

# ---------------------------------------------------------------------------
# The models, content-free (editor/test/editor_records_test.c): the field
# register's types over a SYNTHETIC fields/npc.ini (number with its range,
# bool, name with its kind, param, enum with its words, text; the typed
# guesses for keys the register lacks), a synthetic record over a cache block
# and two overlays (the client-side flag, unknown key, unknown param, a value
# out of range, a word not in its list, known params), the search inside a
# record, "changed only" against a base, Check's job lines formatted and read
# back (Builder_SscFormatDiag / _FormatSummary / _TakeLine).
.PHONY: test-editor-records
test-editor-records:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -UNDEBUG -o $(OBJ_DIR)/editor_records_test $(TORIMEM_SRCS) editor/editor_cfg.c \
		editor/builder/builder_ssc.c editor/test/editor_records_test.c
	./$(OBJ_DIR)/editor_records_test

test-editor: test-editor-records

# The Check job's tool end to end (editor/test/maped_script_check_test.sh): a
# synthetic tree and a stand-in sscompile; an unsaved put staged and its error
# a job line, the put gone, a clean rerun, a work dir in the tree refused.
.PHONY: test-maped-script-check
test-maped-script-check:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/maped_script_check $(TORIMEM_SRCS) editor/builder/builder_ssc.c \
		editor/builder/builder_ssc_main.c
	sh editor/test/maped_script_check_test.sh $(abspath $(OBJ_DIR))/maped_script_check \
		$(abspath $(OBJ_DIR))/tmp/maped_script_check

test-editor: test-maped-script-check

# The panels and the jobs, headless, against the real chrome model and an
# embedded ToriRSMapEd whose catalog indexes a SYNTHETIC content tree and
# whose BUILD runner is the test runner (no make, no sscompile, no cs2, no
# OSRS-Content; editor/test/editor_records_panels_test.c): the Record panel's
# typed rows and editors, its tabs (one per overlay), the search and
# "changed only", the client-pack chip, an unknown key in Problems and its
# click back to the field; the Problems panel from a synthetic BUILD line
# stream (check and cs2 lines, a new build clearing its kind, a silent
# failure) and from the push API, Find and "errors only"; the BUILD check and
# cs2 jobs through the embedded server (their make lines, the unsaved
# documents the server writes for them, the Triggers panel's Check never
# waiting, the refusals); CATALOG_USES over the tree (spawns, placements,
# triggers, scripts, an overlay, a palette, a stamp) in slices; the View
# action. Links the client's objects minus main.o, as
# test-editor-shop-quest-panels does.
.PHONY: test-editor-records-panels
test-editor-records-panels:
	mkdir -p $(OBJ_DIR)
	$(MAKE) $(filter-out $(OBJ_DIR)/main.o,$(OBJS))
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/editor_records_panels_test editor/test/editor_records_panels_test.c \
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/editor_records
	mkdir -p $(OBJ_DIR)/tmp/editor_records
	./$(OBJ_DIR)/editor_records_panels_test $(abspath $(OBJ_DIR)/tmp/editor_records)

test-editor: test-editor-records-panels

.PHONY: test-editor-records-all
test-editor-records-all: test-editor-records test-editor-records-panels
