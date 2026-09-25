# rscache standalone build + test runner.
#
# The client links rscache through src/makefile, which compiles
# rscache_unity.c as a single translation unit. This makefile exists so the
# library can be built and tested on its own:
#
#   make -C 3rd/rscache            # compile the unity TU (syntax/warning gate)
#   make -C 3rd/rscache test       # unit tests
#   make -C 3rd/rscache clean
#
# Unlike src/makefile (which compiles rscache with -w), this build turns
# warnings on. Keep it that way: it is the only place rscache's own diagnostics
# are visible.

CC      ?= cc
THIRD   := ..
BUILD   := build

INCLUDES := -Iinclude -Isrc -I. -I$(THIRD)/bzip -I$(THIRD)/miniz -I$(THIRD)/xteas -I$(THIRD)/ini

# -Wno-unused-parameter: decoders take a revision they may not consult yet.
# -Wno-pointer-sign: archives are char* on disk and uint8_t* in the buffer.
WARNINGS := -Wall -Wextra -Wno-unused-parameter -Wno-pointer-sign

CFLAGS  ?= -std=c11 -g -O0 -fno-omit-frame-pointer
CFLAGS  += $(INCLUDES) $(WARNINGS) -MMD -MP

# The vendored deps are third-party sources; build them at -O3 without our
# warning set, matching how src/makefile treats them.
DEP_CFLAGS := -std=c11 -O3 -g -w $(INCLUDES)

UNITY_OBJ := $(BUILD)/rscache_unity.o
DEP_OBJS  := $(BUILD)/bzip.o $(BUILD)/bzip_encode.o $(BUILD)/miniz.o $(BUILD)/xteas.o
LIB_OBJS  := $(UNITY_OBJ) $(DEP_OBJS)

# One binary per test source. Tests that need a cache directory on disk skip
# themselves when it is absent, so `make test` is always runnable.
TEST_SRCS := $(wildcard test/test_*.c)
TEST_BINS := $(patsubst test/%.c,$(BUILD)/%,$(TEST_SRCS))

# Where the cache corpus lives, for the round-trip suite. Override to point at a
# different set: make test CACHE_ROOT=/path/to/caches
CACHE_ROOT ?= $(THIRD)/..

.PHONY: all test clean tools
.DEFAULT_GOAL := all

all: $(UNITY_OBJ)

tools: all
	$(MAKE) -C tools

$(BUILD):
	mkdir -p $(BUILD)

$(UNITY_OBJ): rscache_unity.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/bzip.o: $(THIRD)/bzip/bzip.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

# The encoder is ours, not vendored, so it builds with the warning set on.
$(BUILD)/bzip_encode.o: $(THIRD)/bzip/bzip_encode.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/miniz.o: $(THIRD)/miniz/miniz.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/xteas.o: $(THIRD)/xteas/xteas.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/%: test/%.c $(LIB_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) -Itest -Itools/common -o $@ $< $(LIB_OBJS)

# The pack-file suite is the exception: `pack/<ns>.pack` is text, so it links
# tools/port_lostcity/lc_pack.c and none of the cache library. It lives here
# rather than under tools/ because the file format is the contract between the
# tools and the content tree, and a contract wants a test that runs on every
# `make test` — not only when someone remembers to build the tools.
$(BUILD)/lc_pack.o: tools/port_lostcity/lc_pack.c | $(BUILD)
	$(CC) $(CFLAGS) -Itools/common -Itools/port_lostcity -c $< -o $@

$(BUILD)/test_pack: test/test_pack.c $(BUILD)/lc_pack.o | $(BUILD)
	$(CC) $(CFLAGS) -Itest -Itools/common -Itools/port_lostcity -o $@ $< $(BUILD)/lc_pack.o

$(BUILD)/cp_pack_ident.o: tools/cachepack/cp_pack_ident.c | $(BUILD)
	$(CC) $(CFLAGS) -Itools/cachepack -Itools/port_lostcity -Itools/common -c $< -o $@

$(BUILD)/test_pack_ident: test/test_pack_ident.c $(BUILD)/lc_pack.o $(BUILD)/cp_pack_ident.o $(LIB_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) -Itest -Itools/common -Itools/port_lostcity -Itools/cachepack -o $@ $< \
		$(BUILD)/lc_pack.o $(BUILD)/cp_pack_ident.o $(LIB_OBJS)

# The field-register suite, for the same reason as the pack suite above:
# `fields/<type>.ini` is the contract between the packer that writes a server band
# and `src/torirsserver/torirs_server_servercodec.c` that reads it, and the two disagreeing
# is a failure with no error attached. A contract wants a test that runs on every
# `make test`, not one that runs when someone remembers to build the tools.
$(BUILD)/ini.o: $(THIRD)/ini/ini.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/cp_fields.o: tools/cachepack/cp_fields.c | $(BUILD)
	$(CC) $(CFLAGS) -Itools/cachepack -c $< -o $@

$(BUILD)/test_fields: test/test_fields.c $(BUILD)/cp_fields.o $(BUILD)/ini.o $(LIB_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) -Itest -Itools/cachepack -o $@ $< $(BUILD)/cp_fields.o $(BUILD)/ini.o \
		$(LIB_OBJS)

# The membership suite, here for the third instance of the same reason:
# `pack/<ns>.client` and `pack/<ns>.server` are the on-disk statement of which
# *entities* have a half on each side, they are authored text, and the migration
# that lands them rests on two emissions of the same content being byte-identical
# (docs/PACK_ENTITY_SPLIT_PLAN.md §4). Text needs no cache, so this never skips.
$(BUILD)/cp_membership.o: tools/cachepack/cp_membership.c | $(BUILD)
	$(CC) $(CFLAGS) -Itools/cachepack -c $< -o $@

$(BUILD)/test_membership: test/test_membership.c $(BUILD)/cp_membership.o | $(BUILD)
	$(CC) $(CFLAGS) -Itest -Itools/common -Itools/cachepack -o $@ $< $(BUILD)/cp_membership.o

# The bzip2 encoder is ours, so it is also checked against the reference bzip2
# binary — our own decoder agreeing with our own encoder is not proof.
$(BUILD)/bzip_interop_helper: test/bzip_interop_helper.c $(BUILD)/bzip_encode.o | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< $(BUILD)/bzip_encode.o

# Run every test binary; fail the target if any one of them fails, but run them
# all first so a single invocation reports every broken codec.
test: $(TEST_BINS) $(BUILD)/bzip_interop_helper
	@status=0; \
	for bin in $(TEST_BINS); do \
		echo "==> $$bin"; \
		case "$$bin" in \
		*test_roundtrip) ./$$bin "$(CACHE_ROOT)" || status=1 ;; \
		*test_cs2) ./$$bin "$(CACHE_ROOT)" || status=1 ;; \
		*test_db_encode) ./$$bin "$(CACHE_ROOT)/cache.osrs239" || status=1 ;; \
		*test_opcode_codec) ./$$bin "$(CACHE_ROOT)/cache.osrs239" || status=1 ;; \
		*test_dat1_encode) ./$$bin "$(CACHE_ROOT)/cache254" || status=1 ;; \
		*test_fields) ./$$bin "$(CACHE_ROOT)/OSRS-Content/osrs239-content" || status=1 ;; \
		*) ./$$bin || status=1 ;; \
		esac; \
	done; \
	echo "==> bzip2 interop"; \
	sh test/test_bzip_interop.sh $(BUILD)/bzip_interop_helper || status=1; \
	echo "==> cachepack fidelity"; \
	sh test/test_cachepack_fidelity.sh $(CACHE_ROOT) || status=1; \
	exit $$status

clean:
	rm -rf $(BUILD)

-include $(wildcard $(BUILD)/*.d)
