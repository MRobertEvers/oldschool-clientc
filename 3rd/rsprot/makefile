# rsprot standalone build + test runner.
#
# The client (once wired in) links rsprot through src/Makefile, compiling
# rsprot_unity.c as a single translation unit — same shape as 3rd/rscache. This
# makefile exists so the library can be built and tested on its own:
#
#   make -C 3rd/rsprot            # compile the unity TU (syntax/warning gate)
#   make -C 3rd/rsprot test       # unit tests
#   make -C 3rd/rsprot clean
#
# This build turns warnings on, same rationale as rscache's makefile: it is the
# only place rsprot's own diagnostics are visible, since src/Makefile compiles
# everything under 3rd/ with -w.

CC    ?= cc
THIRD := ..
BUILD := build

# src/net holds the ISAAC/RSA implementations this library adapts rather than
# re-ports (see src/rsprot_crypto.h); tommath backs the RSA bignum. rsa.h
# includes tommath.h as "3rd/tommath/tommath.h" (repo-root-relative, matching
# how src/Makefile's -I. resolves it), so the repo root needs its own -I too.
INCLUDES := -Iinclude -Isrc -Igen -I. -I$(THIRD)/../src/net -I$(THIRD) -I$(THIRD)/.. -I$(THIRD)/xteas

WARNINGS := -Wall -Wextra -Wno-unused-parameter -Wno-pointer-sign

CFLAGS ?= -std=c11 -g -O0 -fno-omit-frame-pointer
CFLAGS += $(INCLUDES) $(WARNINGS) -MMD -MP

# Vendored/adapted deps build at -O3 without our warning set, matching how
# src/makefile and 3rd/rscache/makefile both treat third-party sources.
DEP_CFLAGS := -std=c11 -O3 -g -w $(INCLUDES)

UNITY_OBJ := $(BUILD)/rsprot_unity.o
DEP_OBJS  := $(BUILD)/isaac.o $(BUILD)/rsa.o $(BUILD)/mpi.o $(BUILD)/xteas.o
LIB_OBJS  := $(UNITY_OBJ) $(DEP_OBJS)

TEST_SRCS := $(wildcard test/test_*.c)
TEST_BINS := $(patsubst test/%.c,$(BUILD)/%,$(TEST_SRCS))

.PHONY: all test clean
.DEFAULT_GOAL := all

all: $(UNITY_OBJ)

$(BUILD):
	mkdir -p $(BUILD)

$(UNITY_OBJ): rsprot_unity.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/isaac.o: $(THIRD)/../src/net/isaac.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/rsa.o: $(THIRD)/../src/net/rsa.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/mpi.o: $(THIRD)/tommath/mpi.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/xteas.o: $(THIRD)/xteas/xteas.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/%: test/%.c $(LIB_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) -Itest -o $@ $< $(LIB_OBJS)

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
