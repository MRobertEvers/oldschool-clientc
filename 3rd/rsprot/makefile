# rsprot standalone build + test runner.
#
# The client (once wired in) links rsprot through src/Makefile, compiling
# rsprot_unity.c as a single translation unit — same shape as 3rd/rscache. This
# makefile exists so the library can be built and tested on its own:
#
#   make -C 3rd/rsprot            # compile the unity TU (syntax/warning gate)
#   make -C 3rd/rsprot test       # unit tests
#   make -C 3rd/rsprot lib        # build/librsprot.a, for a C host
#   make -C 3rd/rsprot ffi        # build/librsprot_ffi.{dylib,so,dll}: ffi/rsprot_ffi.h
#   make -C 3rd/rsprot clean
#
# A host that vendors the library (rsprox-index, rsprox-c) builds it with
# these targets, CFLAGS=-O2 for a release.
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
override CFLAGS += $(INCLUDES) $(WARNINGS) -MMD -MP

UNITY_OBJ   := $(BUILD)/rsprot_unity.o
# rsprot_buf.c is its own object, not part of the unity TU -- see rsprot_unity.c.
BUF_OBJ     := $(BUILD)/rsprot_buf.o
EXEC_OBJS   := $(BUILD)/rsprot_exec.o $(BUILD)/rsprot_storage.o
PACKETS_OBJ := $(BUILD)/rsprot_packets_unity.o
LIB_OBJS    := $(UNITY_OBJ) $(BUF_OBJ) $(EXEC_OBJS) $(PACKETS_OBJ)

LIB_A := $(BUILD)/librsprot.a

UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
FFI_LIB := $(BUILD)/librsprot_ffi.dylib
else ifneq (,$(findstring MINGW,$(UNAME))$(findstring MSYS,$(UNAME)))
FFI_LIB := $(BUILD)/rsprot_ffi.dll
else
FFI_LIB := $(BUILD)/librsprot_ffi.so
endif
# The shared library's objects: the library's sources and the binding, PIC.
FFI_SRCS := rsprot_unity.c src/rsprot_buf.c src/rsprot_exec.c src/rsprot_storage.c \
            rsprot_packets_unity.c ffi/rsprot_ffi.c
FFI_OBJS := $(patsubst %.c,$(BUILD)/pic/%.o,$(notdir $(FFI_SRCS)))

TEST_SRCS := $(wildcard test/test_*.c)
TEST_BINS := $(patsubst test/%.c,$(BUILD)/%,$(TEST_SRCS))

.PHONY: all lib ffi test clean
.DEFAULT_GOAL := all

all: $(LIB_OBJS)

lib: $(LIB_A)

$(LIB_A): $(LIB_OBJS)
	rm -f $@
	ar rcs $@ $(LIB_OBJS)

ffi: $(FFI_LIB)

$(BUILD)/pic:
	mkdir -p $@

define PIC_RULE
$(BUILD)/pic/$(notdir $(1:.c=.o)): $(1) | $(BUILD)/pic
	$$(CC) $$(CFLAGS) -fPIC -w -c $$< -o $$@
endef
$(foreach src,$(FFI_SRCS),$(eval $(call PIC_RULE,$(src))))

$(FFI_LIB): $(FFI_OBJS)
	$(CC) -shared -o $@ $(FFI_OBJS)

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

-include $(wildcard $(BUILD)/*.d $(BUILD)/pic/*.d)
