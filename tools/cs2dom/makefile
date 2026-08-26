# cs2dom — React (TypeScript) to CS2 source and IF3 components.
#
# The tool is Node; this makefile exists so it can be reached the way everything
# else in the tree is reached, and so the CS2 compiler it verifies against is a
# stated dependency rather than something you find out about when a build fails.

NODE ?= node
NPM ?= npm
PYTHON ?= python3
PROJECT ?= example
REPO_ROOT := $(abspath $(CURDIR)/../..)
CS2 := $(REPO_ROOT)/3rd/rscache/tools/cs2/cs2
RSCACHE := $(REPO_ROOT)/3rd/rscache

.PHONY: all install test dev build build-dry check cachegen ops bake ship clean \
	generated generated-check test-aot corpus-aot bench-rebuild bench-phase6 \
	roundtrip-if roundtrip-chain \
	emit-reference emit-parity emit-reference-all emit-parity-all \
	host-coverage dev-canvas client-web verify-cmd-wire smoke-client

all: test

node_modules: package.json
	$(NPM) install
	@touch node_modules

install: node_modules

# Generated CS2 is checked against the real compiler, so the tests need it built.
$(CS2):
	$(MAKE) -C $(REPO_ROOT)/3rd/rscache/tools cs2

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
HOST_REQUEST_KINDS := $(REPO_ROOT)/src/cs2vm2/cs2vm2_host_request_kinds.def
PARK_SOURCES := $(REPO_ROOT)/src/game/task_cs2_run.c $(HOST_REQUEST_KINDS)
SURFACE_SOURCES := $(RSCACHE)/src/cs2/cs2_command.gen.h $(RSCACHE)/src/cs2/cs2_types.h

$(PARK_TABLE): scripts/gen_host_park.py $(PARK_SOURCES)
	$(PYTHON) scripts/gen_host_park.py

$(HOST_SURFACE): scripts/gen_host_surface.py $(PARK_TABLE) $(SURFACE_SOURCES)
	$(PYTHON) scripts/gen_host_surface.py

generated: $(PARK_TABLE) $(HOST_SURFACE)

generated-check: $(PARK_TABLE) $(HOST_SURFACE)
	$(PYTHON) scripts/gen_host_park.py --check
	$(PYTHON) scripts/gen_host_surface.py --check

# The dev server. The preview is the official client compiled to WebAssembly,
# so the client is a build dependency rather than a thing to remember: a server
# that comes up and serves a blank iframe is a worse failure than one that says
# what to build.
CLIENT_WASM := $(REPO_ROOT)/build-web/torirs.wasm
IO_SERVER := $(REPO_ROOT)/src/build/io_server

$(CLIENT_WASM):
	$(MAKE) -C $(REPO_ROOT)/src web

$(IO_SERVER):
	$(MAKE) -C $(REPO_ROOT)/src io-server

# Rebuild the client from this directory when its sources moved.
client-web:
	$(MAKE) -C $(REPO_ROOT)/src web

dev-canvas: node_modules generated $(CS2) $(CLIENT_WASM) $(IO_SERVER)
	$(NODE) bin/cs2dom.js dev-canvas --project $(PROJECT)

# The AOT back end's own suites. No cache and no browser needed.
test-aot: generated
	$(NODE) test/cs2_js_emit_test.js
	$(NODE) test/cmd_frames_test.js
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

# ---- the client, as the preview -----------------------------------------
#
# Two gates on the seam between this tool and the client, both needing a built
# client and therefore not part of `make test`.
#
# verify-cmd-wire answers "does C read the frames this tool writes?" -- the
# encoder here and CmdBus_Push* in the client are two implementations of one
# byte layout, and test/cmd_frames_test.js can only check one of them against
# octets someone typed. This hands the real bytes to the real client.
#
# smoke-client answers "does the preview draw?" -- it replaces the emit and
# pixel parity harnesses, which existed to check a renderer this tool no longer
# has. It is a LIVENESS check, deliberately: correctness belongs to the client
# now, and a second opinion about it here would be the mistake being undone.
verify-cmd-wire: $(TORIRS)
	$(NODE) scripts/verify_cmd_wire.mjs --client $(TORIRS) --cache $(CACHE) --rev $(REV)

smoke-client: node_modules $(CLIENT_WASM) $(IO_SERVER)
	$(NODE) scripts/smoke_client.mjs --cache $(CACHE) --rev $(REV) --content $(CONTENT)

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

dev: dev-canvas

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
