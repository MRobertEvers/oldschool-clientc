# cs2dom — React (TypeScript) to CS2 source and IF3 components.
#
# The tool is Node; this makefile exists so it can be reached the way everything
# else in the tree is reached, and so the CS2 compiler it verifies against is a
# stated dependency rather than something you find out about when a build fails.

NODE ?= node
NPM ?= npm
CC ?= cc
EMCC ?= emcc
PYTHON ?= python3
PROJECT ?= example
REPO_ROOT := $(abspath $(CURDIR)/../..)
CS2 := $(REPO_ROOT)/3rd/rscache/tools/cs2/cs2
RSCACHE := $(REPO_ROOT)/3rd/rscache

.PHONY: all install test test-wasm-core wasm react-runtime browser-runtime dev build build-dry check cachegen ops bake ship clean \
	generated test-aot corpus-aot bench-rebuild bench-phase6 roundtrip-if roundtrip-chain \
	emit-reference emit-parity emit-reference-all emit-parity-all \
	host-coverage dev-canvas dev-legacy test-legacy

all: test

node_modules: package.json
	$(NPM) install
	@touch node_modules

install: node_modules

# Generated CS2 is checked against the real compiler, so the tests need it built.
$(CS2):
	$(MAKE) -C $(REPO_ROOT)/3rd/rscache/tools cs2

# The default test path is the JavaScript-native runtime's. The old stack's
# suites moved to `test-legacy` — they exercise a C/WASM bridge, an engine
# router and a worker protocol that nothing in this build reaches any more.
test: test-aot roundtrip-if

# ---- the JavaScript-native CS2 back end ---------------------------------
#
# Three generated tables, all derived from sources that already exist rather
# than restated by hand: what can park (the C client's own yield planner), what
# the host must answer (the decompiler's command table), and the syntax trees
# the emitter lowers (`cs2 --emit ast-json`). `generated-check` is the CI gate
# that says one of those sources moved and the table did not follow.

PARK_TABLE := src/generated/cs2_host_park.js
HOST_SURFACE := src/generated/cs2_host_surface.js
PARK_SOURCES := $(REPO_ROOT)/src/game/task_cs2_run.c $(CS2W_SCHEMA_SOURCE)
SURFACE_SOURCES := $(RSCACHE)/src/cs2/cs2_command.gen.h $(RSCACHE)/src/cs2/cs2_types.h

$(PARK_TABLE): scripts/gen_host_park.py $(PARK_SOURCES)
	$(PYTHON) scripts/gen_host_park.py

$(HOST_SURFACE): scripts/gen_host_surface.py $(PARK_TABLE) $(SURFACE_SOURCES)
	$(PYTHON) scripts/gen_host_surface.py

generated: $(PARK_TABLE) $(HOST_SURFACE)

generated-check: $(PARK_TABLE) $(HOST_SURFACE)
	$(PYTHON) scripts/gen_host_park.py --check
	$(PYTHON) scripts/gen_host_surface.py --check

# The JavaScript-native runtime's dev server: canvas preview, no worker, no
# WASM. This is the default `dev` target; `dev-legacy` still reaches the old
# C/WASM + React-DOM stack while it is being retired.
dev-canvas: node_modules generated $(CS2)
	$(NODE) bin/cs2dom.js dev-canvas --project $(PROJECT)

# The AOT back end's own suites. No cache and no browser needed.
test-aot: generated
	$(NODE) test/cs2_js_emit_test.js
	$(NODE) test/uitree_test.js
	$(NODE) test/host_services_test.js
	$(NODE) test/transmit_pump_test.js
	$(NODE) test/layout_test.js
	$(NODE) test/emit_hit_test.js
	$(NODE) test/painter_test.js
	$(NODE) test/session_test.js
	$(NODE) test/assets_test.js
	$(NODE) test/asset_loader_test.js
	$(NODE) test/round_trip_test.js
	$(NODE) test/dev_canvas_test.js
	$(NODE) test/content_assets_test.js
	$(NODE) test/export_test.js
	$(NODE) test/tsx_import_test.js
	$(NODE) test/model_source_test.js
	$(NODE) test/emit_parity_test.js
	$(NODE) test/host_surface_arity_test.js
	$(NODE) test/host_client_test.js
	$(NODE) test/cs2_end_to_end_test.js

# The standing corpus gate: every script the decompiler can describe must lower
# to JavaScript the engine accepts, with no undeclared local. Needs a cache.
CACHE ?= $(REPO_ROOT)/cache.osrs239
REV ?= osrs239
CS2_NAMES ?= $(REPO_ROOT)/../cs2/src/main/resources/org/runestar/cs2
AST_DIR ?= build/ast
AST_STAMP := $(AST_DIR)/.stamp

# `cs2 decompile` exits non-zero when ANY script fails, and one always does:
# script 0 of cache.osrs239 has never decoded (its trailer does not parse), a
# pre-existing gap the source path shares. Whether the decompiler covers the
# cache is `cs2 roundtrip`'s question and it is already a standing gate; this
# target's question is narrower — whatever trees exist must lower — so the
# decompile is input preparation and `emit_corpus_js.mjs` is the gate.
$(AST_STAMP): $(CS2)
	mkdir -p $(AST_DIR)
	-$(CS2) decompile --cache $(CACHE) --rev $(REV) --names $(CS2_NAMES) \
		--emit ast-json --quiet --out $(AST_DIR)
	@test -n "$$(ls -A $(AST_DIR)/*.json 2>/dev/null)" || \
		{ echo "no syntax trees produced; is $(CACHE) present?" >&2; exit 1; }
	@touch $@

corpus-aot: generated $(AST_STAMP)
	$(NODE) scripts/emit_corpus_js.mjs $(AST_DIR) --quiet

# The round trip's real gate: every .if in the content tree must survive a
# parse-and-write unchanged, or a diff after an edit shows more than the edit.
CONTENT ?= $(REPO_ROOT)/OSRS-Content/osrs239-content

roundtrip-if:
	$(NODE) scripts/verify_if_roundtrip.mjs $(CONTENT)

# The whole chain, end to end: edit an interface, bake it, read the change back
# out of the packed bytes. Slow (it copies a content tree and a cache), so it is
# its own target rather than part of `make test`.
CHAIN_SCRATCH ?= $(CURDIR)/build/chain

roundtrip-chain: $(CS2)
	$(MAKE) -C $(REPO_ROOT)/3rd/rscache/tools cachepack
	rm -rf $(CHAIN_SCRATCH)
	mkdir -p $(CHAIN_SCRATCH)
	$(NODE) scripts/verify_chain.mjs $(CHAIN_SCRATCH)

# ---- parity against the C client ----------------------------------------
#
# `emit-reference` runs the headless client over a set of interfaces and stores
# each one's draw list under test/fixtures/emit. `emit-parity` bakes the same
# interfaces here, runs their onload closures, lays out, walks emit and diffs.
#
# Two targets rather than one because they have different costs and different
# dependencies: the capture needs a built client and a cache and takes seconds
# per interface, and the comparison needs neither. The reference is checked in,
# so the comparison is the one you run.
#
# NEITHER is part of `make test`. The comparison is not green — it is a
# MEASUREMENT, and the numbers it prints are the state of the port. Wiring an
# unfinished measurement into the default target teaches people to ignore a red
# build, and the day it finally matters nobody looks.
TORIRS ?= $(REPO_ROOT)/src/torirs
EMIT_INTERFACES ?= 600,12,162,218

$(TORIRS):
	$(MAKE) -C $(REPO_ROOT)/src torirs

emit-reference: $(TORIRS)
	$(NODE) scripts/capture_emit_reference.mjs --cache $(CACHE) --rev $(REV) \
		--client $(TORIRS) --interfaces $(EMIT_INTERFACES)

# The whole pack, which is the number that means anything: 881 interfaces
# produce a draw list (the other 88 are meant to be mounted inside another).
#
# Two steps, because the first needs the C client and a cache and the second
# does not. The capture is reproducible — a bounded run ticks its game logic
# once per frame — so it only has to be redone when the CLIENT changes.
#
#   make emit-reference-all        # ~50s, writes EMIT_ALL_DIR
#   make emit-parity-all           # ~6s, prints `matching` and `differing`
#
# `--jobs` is how many clients run at once; lower it on a small machine.
EMIT_ALL_DIR ?= build/emit-reference
EMIT_ALL_JOBS ?= 10
EMIT_ALL_FRAMES ?= 3

emit-reference-all: $(TORIRS)
	$(NODE) scripts/capture_emit_reference.mjs --cache $(CACHE) --rev $(REV) \
		--client $(TORIRS) --all --frames $(EMIT_ALL_FRAMES) \
		--jobs $(EMIT_ALL_JOBS) --out $(EMIT_ALL_DIR)

emit-parity-all: generated $(CS2)
	$(NODE) scripts/verify_emit_parity.mjs --content $(CONTENT) --cache $(CACHE) \
		--rev $(REV) --cs2 $(CS2) --reference $(EMIT_ALL_DIR)

# What the host answers, measured against what the REFERENCE answers rather
# than against the whole decompiler surface. See the script's header for why
# the second number is the misleading one.
host-coverage: generated
	$(NODE) scripts/host_coverage_report.mjs

emit-parity: generated $(CS2)
	$(NODE) scripts/verify_emit_parity.mjs --content $(CONTENT) --cache $(CACHE) \
		--rev $(REV) --cs2 $(CS2)

bench-rebuild: generated
	$(NODE) scripts/bench_mass_rebuild.mjs
	$(NODE) scripts/bench_full_frame.mjs

# Phase 6 of the plan is gated on a profile: "add only if profiles still
# identify interpreter dispatch as material". This is that profile.
bench-phase6: generated
	$(NODE) scripts/bench_dispatch_share.mjs

# ---- production C CS2VM2 in WebAssembly ---------------------------------

CS2W_SCHEMA_SOURCE := $(REPO_ROOT)/src/cs2vm2/cs2vm2_host_request_kinds.def
CS2W_SCHEMA := wasm/cs2vm_host_schema.gen.h
CS2W_REQUESTS_JS := src/cs2_host_requests.js
CS2W_REQUESTS_TS := src/generated/cs2_host.ts
CS2W_COMMANDS_JS := src/cs2_commands.js
CS2W_DECODE_OPERANDS := $(REPO_ROOT)/src/osrs/rscache/dat2a/dat2a_cs2_opcode_decode.c
CS2W_NATIVE_DECODER := $(RSCACHE)/src/datatypes/clientscript.c
CS2W_EXECUTABLE_SEMANTICS := wasm/cs2_host_executable_semantics.json
CS2W_BUILD := build/cs2vm
CS2W_WEB := web/cs2vm_wasm.js
CS2W_WASM := web/cs2vm_wasm.wasm
REACT_WEB := web/react_browser_runtime.js
TS_VM_WEB := web/cs2_vm_core.js
TS_DECODER_WEB := web/cs2_bytecode_decoder.js
TS_ENGINE_WEB := web/cs2_engine_router.js
TS_COVERAGE_WEB := web/cs2_backend_coverage.js
CS2W_INCLUDES := -Iwasm -I$(REPO_ROOT)/src -I$(RSCACHE)/include -I$(RSCACHE)/src
CS2W_CFLAGS := -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra \
	-Wno-unused-parameter -Wno-unused-variable -Wno-unused-function $(CS2W_INCLUDES)
CS2W_CORE := \
	wasm/cs2vm_wasm.c \
	$(REPO_ROOT)/src/cs2vm2/cs2vm2.c \
	$(REPO_ROOT)/src/cs2vm2/cs2vm2_script.c \
	$(REPO_ROOT)/src/cs2vm2/cs2vm2_trigger_args.c \
	$(REPO_ROOT)/src/cs2vm2/cs2vm2_strpool.c \
	$(REPO_ROOT)/src/cs2vm2/cs2_opcode_meta.c \
	$(REPO_ROOT)/src/engine/cs2vm2_script_from_rscache.c \
	$(REPO_ROOT)/src/engine/cs2_opcode_dialect.c \
	$(REPO_ROOT)/src/perf/torirs_perf.c \
	$(RSCACHE)/src/datatypes/clientscript.c \
	$(RSCACHE)/src/datatypes/cs2_script.c \
	$(RSCACHE)/src/datatypes/cs2_opcode_decode.c \
	$(RSCACHE)/src/rsbuffer.c \
	$(RSCACHE)/src/rscache_profile.c
CS2W_HEADERS := \
	wasm/cs2vm_wasm.h $(CS2W_SCHEMA) \
	$(wildcard $(REPO_ROOT)/src/cs2vm2/*.h) \
	$(REPO_ROOT)/src/engine/cs2_opcode_dialect.h \
	$(REPO_ROOT)/src/engine/cs2vm2_script_from_rscache.h \
	$(RSCACHE)/src/datatypes/clientscript.h \
	$(RSCACHE)/src/datatypes/cs2_script.h

$(CS2W_SCHEMA): wasm/gen_host_schema.py $(CS2W_SCHEMA_SOURCE)
	$(PYTHON) $^ $@

$(CS2W_REQUESTS_JS): wasm/gen_host_schema.py $(CS2W_SCHEMA_SOURCE)
	$(PYTHON) $^ $@ --format js

$(CS2W_REQUESTS_TS): wasm/gen_host_schema.py $(CS2W_SCHEMA_SOURCE) \
		$(CS2W_COMMANDS_JS) $(CS2W_DECODE_OPERANDS) $(CS2W_NATIVE_DECODER) \
		$(CS2W_EXECUTABLE_SEMANTICS)
	$(PYTHON) wasm/gen_host_schema.py $(CS2W_SCHEMA_SOURCE) $@ --format ts \
		--commands $(CS2W_COMMANDS_JS) --operands $(CS2W_DECODE_OPERANDS) \
		--native-decoder $(CS2W_NATIVE_DECODER) \
		--executable-semantics $(CS2W_EXECUTABLE_SEMANTICS)

$(CS2W_BUILD):
	mkdir -p $@

web:
	mkdir -p $@

$(CS2W_BUILD)/cs2vm_wasm_test: wasm/cs2vm_wasm_test.c $(CS2W_CORE) $(CS2W_HEADERS) | $(CS2W_BUILD)
	$(CC) -O0 -g $(CS2W_CFLAGS) -o $@ wasm/cs2vm_wasm_test.c $(CS2W_CORE) -lm

test-wasm-core: $(CS2W_BUILD)/cs2vm_wasm_test
	./$(CS2W_BUILD)/cs2vm_wasm_test

# ES-module factory: `import createCS2VMModule from './cs2vm_wasm.js'`.
# The caller supplies synchronous `cs2HostExec` and locateFile options.
$(CS2W_WEB): $(CS2W_CORE) $(CS2W_HEADERS) | web
	$(EMCC) -O3 -flto $(CS2W_CFLAGS) -w -o $@ $(CS2W_CORE) \
		-sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createCS2VMModule \
		-sENVIRONMENT=web -sFILESYSTEM=0 -sNO_EXIT_RUNTIME=1 \
		-sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=16777216 -sSTACK_SIZE=1048576 \
		-sEXPORTED_FUNCTIONS='["_malloc","_free","_cs2w_request_field_offset","_cs2w_request_field_capacity","_cs2w_request_field_stride","_cs2w_request_field_count_offset","_cs2w_request_pointer_size"]' \
		-sEXPORTED_RUNTIME_METHODS='["cwrap","UTF8ToString","HEAPU8","HEAP32","HEAPU32"]' \
		-sASSERTIONS=0

wasm: $(CS2W_WEB) $(CS2W_REQUESTS_JS)
	@test -f $(CS2W_WASM) || $(MAKE) -B $(CS2W_WEB)

# GNU make 3.81 treats an ordinary multi-target rule as one independent recipe
# per target. Since the bundler writes both files, a clean/forced parallel build
# would otherwise start two esbuild processes racing over the same outputs.
# Make the React bundle the portable primary target; the ordered VM target also
# repairs a checkout where only its generated file was removed.
$(REACT_WEB): scripts/build_react_runtime.js src/react_browser_runtime.js \
		src/react_stage_mount.js src/react_tree_mount.js src/react_tree_renderer.js \
		src/cs2_vm_core.ts src/cs2_bytecode_decoder.ts src/cs2_engine_router.ts \
		src/cs2_backend_coverage.ts \
		src/generated/cs2_opcode_semantics.ts src/generated/cs2_wire_opcodes.ts \
		src/generated/cs2_host.ts package-lock.json | web node_modules
	$(NODE) scripts/build_react_runtime.js

$(TS_VM_WEB): $(REACT_WEB)
	@test -f $@ || $(NODE) scripts/build_react_runtime.js

$(TS_DECODER_WEB): $(REACT_WEB)
	@test -f $@ || $(NODE) scripts/build_react_runtime.js

$(TS_ENGINE_WEB): $(REACT_WEB)
	@test -f $@ || $(NODE) scripts/build_react_runtime.js

$(TS_COVERAGE_WEB): $(REACT_WEB)
	@test -f $@ || $(NODE) scripts/build_react_runtime.js

react-runtime: $(REACT_WEB) $(TS_VM_WEB) $(TS_DECODER_WEB) $(TS_ENGINE_WEB) $(TS_COVERAGE_WEB)

browser-runtime: wasm react-runtime

dev: dev-canvas

# The retiring stack. Kept reachable while the files it needs are still on
# disk; nothing in the default build path depends on it any more.
dev-legacy: node_modules browser-runtime
	$(NODE) bin/cs2dom.js dev --project $(PROJECT)

test-legacy: node_modules $(CS2) src/cs2_host_requests.js src/generated/cs2_host.ts
	$(NODE) test/run_all_tests.js

build: node_modules $(CS2)
	$(NODE) bin/cs2dom.js build --project $(PROJECT)

build-dry: node_modules $(CS2)
	$(NODE) bin/cs2dom.js build --project $(PROJECT) --dry-run

check: node_modules
	$(NODE) bin/cs2dom.js check --project $(PROJECT)

cachegen: node_modules
	$(NODE) bin/cs2dom.js cachegen --project $(PROJECT)

ops: node_modules
	$(NODE) bin/cs2dom.js ops

# A .cs2 packed into no cache is an edit that ships nowhere.
bake:
	$(MAKE) -C $(REPO_ROOT)/src torirsserver-cache

ship: build bake

clean:
	rm -rf node_modules
