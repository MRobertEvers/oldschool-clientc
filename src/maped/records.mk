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
#       MAPED_CS2_CACHE=<cache> MAPED_CACHE_REV=osrs242 MAPED_CS2_SCRIPTS=<tree>/scripts
#       `cs2 compile` of the one script the server wrote to
#       MAPED_CS2_DIR/src/<name>.cs2, into MAPED_CS2_DIR/out. --names is
#       MAPED_CS2_DIR/names, which maped_cs2_names
#       (editor/iface/iface_cs2_names_main.c) writes first: RuneStar's tables
#       (RUNESTAR_CS2_NAMES, when they are on this machine) linked, and
#       script-names.tsv from MAPED_CS2_SCRIPTS' .cs2 headers, so a ~call to a
#       content-named script resolves. Without MAPED_CS2_SCRIPTS, RuneStar's
#       tables alone.
MAPED_CHECK_DIR ?=
MAPED_CS2_DIR ?=
MAPED_CS2_SCRIPT ?=
MAPED_CS2_CACHE ?=
MAPED_CS2_SCRIPTS ?=
.PHONY: maped-script-check maped-cs2-compile
maped-script-check: sscompile
	@[ -n "$(MAPED_CHECK_DIR)" ] || { echo "maped-script-check: MAPED_CHECK_DIR=<dir> is required" >&2; exit 2; }
	@mkdir -p $(OBJ_DIR) $(MAPED_CHECK_DIR)
	@[ $(OBJ_DIR)/maped_script_check -nt editor/builder/builder_ssc.c ] \
		&& [ $(OBJ_DIR)/maped_script_check -nt editor/builder/builder_ssc_main.c ] \
		|| $(CC) $(CFLAGS) -o $(OBJ_DIR)/maped_script_check $(TORIMEM_SRCS) editor/builder/builder_ssc.c \
			editor/builder/builder_ssc_main.c platform/launch_sessions.c $(MAPED_TEST_LDFLAGS)
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
ifneq ($(MAPED_CS2_SCRIPTS),)
	@mkdir -p $(OBJ_DIR)
	@[ $(OBJ_DIR)/maped_cs2_names -nt editor/iface/iface_model.c ] \
		&& [ $(OBJ_DIR)/maped_cs2_names -nt editor/iface/iface_cs2_names_main.c ] \
		|| $(CC) $(CFLAGS) -o $(OBJ_DIR)/maped_cs2_names $(TORIMEM_SRCS) editor/iface/iface_model.c \
			editor/iface/iface_cs2_names_main.c $(MAPED_TEST_LDFLAGS)
	@$(abspath $(OBJ_DIR))/maped_cs2_names $(MAPED_CS2_SCRIPTS) "$(wildcard $(RUNESTAR_CS2_NAMES))" \
		$(MAPED_CS2_DIR)/names
endif
	@echo "cs2: compiling $(MAPED_CS2_SCRIPT).cs2"
	$(CS2_BIN) compile --src $(MAPED_CS2_DIR)/src \
		--cache $(if $(MAPED_CS2_CACHE),$(MAPED_CS2_CACHE),$(REPO_ROOT)/cache.$(if $(MAPED_CACHE_REV),$(MAPED_CACHE_REV),osrs242)) \
		--rev $(if $(MAPED_CACHE_REV),$(MAPED_CACHE_REV),osrs242) \
		$(if $(MAPED_CS2_SCRIPTS),--names $(MAPED_CS2_DIR)/names,$(if $(wildcard $(RUNESTAR_CS2_NAMES)),--names $(RUNESTAR_CS2_NAMES),)) \
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
		editor/builder/builder_ssc.c editor/test/editor_records_test.c $(MAPED_TEST_LDFLAGS)
	./$(OBJ_DIR)/editor_records_test

test-editor: test-editor-records

# The Check job's tool end to end (editor/test/maped_script_check_test.sh): a
# synthetic tree and a stand-in sscompile; an unsaved put staged and its error
# a job line, the put gone, a clean rerun, a work dir in the tree refused.
.PHONY: test-maped-script-check
ifeq ($(OS),Windows_NT)
test-maped-script-check:
	@echo "test-maped-script-check: SKIPPED on Windows: a POSIX test harness (fork, BSD sockets, /bin/sh fakes for the client and the game server): run it on Linux or macOS; on Windows the paths it covers were run by hand (docs/MAPED_STATUS.md, Windows run)"
else
test-maped-script-check:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/maped_script_check $(TORIMEM_SRCS) editor/builder/builder_ssc.c \
		editor/builder/builder_ssc_main.c platform/launch_sessions.c $(MAPED_TEST_LDFLAGS)
	sh editor/test/maped_script_check_test.sh $(abspath $(OBJ_DIR))/maped_script_check \
		$(abspath $(OBJ_DIR))/tmp/maped_script_check
endif

test-editor: test-maped-script-check

# The CS2 job's names step (editor/iface/iface_cs2_names_main.c), on a
# synthetic scripts dir and a synthetic RuneStar table: the content's script
# names in script-names.tsv, RuneStar's other tables linked beside it, its own
# script-names.tsv not.
.PHONY: test-maped-cs2-names
test-maped-cs2-names:
	mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -o $(OBJ_DIR)/maped_cs2_names $(TORIMEM_SRCS) editor/iface/iface_model.c \
		editor/iface/iface_cs2_names_main.c $(MAPED_TEST_LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/maped_cs2_names
	mkdir -p $(OBJ_DIR)/tmp/maped_cs2_names/scripts $(OBJ_DIR)/tmp/maped_cs2_names/runestar
	printf '// 5344\n[proc,openurl_raw_handle_event_5344](int $$a)\nreturn;\n' \
		> $(OBJ_DIR)/tmp/maped_cs2_names/scripts/openurl_raw_handle_event_5344.cs2
	printf '// 12\n[clientscript,bank_sort]\nreturn;\n' > $(OBJ_DIR)/tmp/maped_cs2_names/scripts/bank_sort.cs2
	printf '1\tobj_one\n' > $(OBJ_DIR)/tmp/maped_cs2_names/runestar/obj-names.tsv
	printf '5344\t[proc,script5344]\n' > $(OBJ_DIR)/tmp/maped_cs2_names/runestar/script-names.tsv
	./$(OBJ_DIR)/maped_cs2_names $(OBJ_DIR)/tmp/maped_cs2_names/scripts $(OBJ_DIR)/tmp/maped_cs2_names/runestar \
		$(OBJ_DIR)/tmp/maped_cs2_names/names
	grep -qx '5344	\[proc,openurl_raw_handle_event_5344\]' $(OBJ_DIR)/tmp/maped_cs2_names/names/script-names.tsv
	grep -qx '12	\[clientscript,bank_sort\]' $(OBJ_DIR)/tmp/maped_cs2_names/names/script-names.tsv
	if grep -q script5344 $(OBJ_DIR)/tmp/maped_cs2_names/names/script-names.tsv; then exit 1; fi
	[ -L $(OBJ_DIR)/tmp/maped_cs2_names/names/obj-names.tsv ] || { [ "$(OS)" = "Windows_NT" ] && [ -f $(OBJ_DIR)/tmp/maped_cs2_names/names/obj-names.tsv ]; }
	./$(OBJ_DIR)/maped_cs2_names $(OBJ_DIR)/tmp/maped_cs2_names/scripts "" $(OBJ_DIR)/tmp/maped_cs2_names/names_only
	[ -f $(OBJ_DIR)/tmp/maped_cs2_names/names_only/script-names.tsv ] \
		&& [ ! -e $(OBJ_DIR)/tmp/maped_cs2_names/names_only/obj-names.tsv ]
	@echo "test-maped-cs2-names: ok"

test-editor: test-maped-cs2-names

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
		$(filter-out $(OBJ_DIR)/main.o,$(OBJS)) $(LDFLAGS) $(MAPED_TEST_LDFLAGS)
	rm -rf $(OBJ_DIR)/tmp/editor_records
	mkdir -p $(OBJ_DIR)/tmp/editor_records
	./$(OBJ_DIR)/editor_records_panels_test $(abspath $(OBJ_DIR)/tmp/editor_records)

test-editor: test-editor-records-panels

.PHONY: test-editor-records-all
test-editor-records-all: test-editor-records test-editor-records-panels
