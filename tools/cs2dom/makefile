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

.PHONY: all install test test-wasm-core wasm dev build build-dry check cachegen ops bake ship clean

all: test

node_modules: package.json
	$(NPM) install
	@touch node_modules

install: node_modules

# Generated CS2 is checked against the real compiler, so the tests need it built.
$(CS2):
	$(MAKE) -C $(REPO_ROOT)/3rd/rscache/tools cs2

test: node_modules $(CS2) src/cs2_host_requests.js
	$(NODE) test/run_all_tests.js

# ---- production C CS2VM2 in WebAssembly ---------------------------------

CS2W_SCHEMA_SOURCE := $(REPO_ROOT)/src/cs2vm2/cs2vm2_host_request_kinds.def
CS2W_SCHEMA := wasm/cs2vm_host_schema.gen.h
CS2W_REQUESTS_JS := src/cs2_host_requests.js
CS2W_BUILD := build/cs2vm
CS2W_WEB := web/cs2vm_wasm.js
CS2W_WASM := web/cs2vm_wasm.wasm
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
	$(PYTHON) $(CS2W_SCHEMA_SOURCE) $@ --format js

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

dev: node_modules wasm
	$(NODE) bin/cs2dom.js dev --project $(PROJECT)

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
