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

INCLUDES := -Iinclude -Isrc -I. -I$(THIRD)/bzip -I$(THIRD)/miniz -I$(THIRD)/xteas

# -Wno-unused-parameter: decoders take a revision they may not consult yet.
# -Wno-pointer-sign: archives are char* on disk and uint8_t* in the buffer.
WARNINGS := -Wall -Wextra -Wno-unused-parameter -Wno-pointer-sign

CFLAGS  ?= -std=c11 -g -O0 -fno-omit-frame-pointer
CFLAGS  += $(INCLUDES) $(WARNINGS) -MMD -MP

# The vendored deps are third-party sources; build them at -O3 without our
# warning set, matching how src/makefile treats them.
DEP_CFLAGS := -std=c11 -O3 -g -w $(INCLUDES)

UNITY_OBJ := $(BUILD)/rscache_unity.o
DEP_OBJS  := $(BUILD)/bzip.o $(BUILD)/miniz.o $(BUILD)/xteas.o
LIB_OBJS  := $(UNITY_OBJ) $(DEP_OBJS)

# One binary per test source. Tests that need a cache directory on disk skip
# themselves when it is absent, so `make test` is always runnable.
TEST_SRCS := $(wildcard test/test_*.c)
TEST_BINS := $(patsubst test/%.c,$(BUILD)/%,$(TEST_SRCS))

.PHONY: all test clean
.DEFAULT_GOAL := all

all: $(UNITY_OBJ)

$(BUILD):
	mkdir -p $(BUILD)

$(UNITY_OBJ): rscache_unity.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/bzip.o: $(THIRD)/bzip/bzip.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/miniz.o: $(THIRD)/miniz/miniz.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/xteas.o: $(THIRD)/xteas/xteas.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/%: test/%.c $(LIB_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) -Itest -o $@ $< $(LIB_OBJS)

# Run every test binary; fail the target if any one of them fails, but run them
# all first so a single invocation reports every broken codec.
test: $(TEST_BINS)
	@status=0; \
	for bin in $(TEST_BINS); do \
		echo "==> $$bin"; \
		./$$bin || status=1; \
	done; \
	exit $$status

clean:
	rm -rf $(BUILD)

-include $(wildcard $(BUILD)/*.d)
