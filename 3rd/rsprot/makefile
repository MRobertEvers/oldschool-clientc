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

# The library depends on nothing outside itself.
INCLUDES := -Iinclude -Isrc -Igen -I.

WARNINGS := -Wall -Wextra -Wno-unused-parameter -Wno-pointer-sign

CFLAGS ?= -std=c11 -g -O0 -fno-omit-frame-pointer
CFLAGS += $(INCLUDES) $(WARNINGS) -MMD -MP

UNITY_OBJ   := $(BUILD)/rsprot_unity.o
# rsprot_buf.c is its own object, not part of the unity TU -- see rsprot_unity.c.
BUF_OBJ     := $(BUILD)/rsprot_buf.o
EXEC_OBJS   := $(BUILD)/rsprot_exec.o $(BUILD)/rsprot_storage.o
PACKETS_OBJ := $(BUILD)/rsprot_packets_unity.o
LIB_OBJS    := $(UNITY_OBJ) $(BUF_OBJ) $(EXEC_OBJS) $(PACKETS_OBJ)

TEST_SRCS := $(wildcard test/test_*.c)
TEST_BINS := $(patsubst test/%.c,$(BUILD)/%,$(TEST_SRCS))

.PHONY: all test clean
.DEFAULT_GOAL := all

all: $(LIB_OBJS)

$(BUILD):
	mkdir -p $(BUILD)

$(BUF_OBJ): src/rsprot_buf.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(UNITY_OBJ): rsprot_unity.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/rsprot_%.o: src/rsprot_%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

# The codecs and streams: generated code, built without the warning set.
$(PACKETS_OBJ): rsprot_packets_unity.c | $(BUILD)
	$(CC) $(CFLAGS) -w -c $< -o $@

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
