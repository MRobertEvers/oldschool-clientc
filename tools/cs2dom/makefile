# cs2dom — React (TypeScript) to CS2 source and IF3 components.
#
# The tool is Node; this makefile exists so it can be reached the way everything
# else in the tree is reached, and so the CS2 compiler it verifies against is a
# stated dependency rather than something you find out about when a build fails.

NODE ?= node
NPM ?= npm
PROJECT ?= example
REPO_ROOT := $(abspath $(CURDIR)/../..)
CS2 := $(REPO_ROOT)/3rd/rscache/tools/cs2/cs2

.PHONY: all install test dev build build-dry check cachegen ops bake ship clean

all: test

node_modules: package.json
	$(NPM) install
	@touch node_modules

install: node_modules

# Generated CS2 is checked against the real compiler, so the tests need it built.
$(CS2):
	$(MAKE) -C $(REPO_ROOT)/3rd/rscache/tools cs2

test: node_modules $(CS2)
	$(NODE) test/run_tests.js

dev: node_modules
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
