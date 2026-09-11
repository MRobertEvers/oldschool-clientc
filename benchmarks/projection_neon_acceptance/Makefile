CC ?= cc

TORIDRAW_ROOT ?= ../../3rd/toridraw
TARGET ?= bench_projection_neon_acceptance
SANITIZE_TARGET ?= $(TARGET)_sanitize
DISPATCH_TARGET ?= bench_projection_native_dispatch
BASELINE_DISPATCH_TARGET ?= bench_projection_baseline_dispatch
BASELINE_TORIDRAW_ROOT ?=
DISPATCH_CFLAGS ?= -O3 -std=c11 -Wall -Wextra \
	-Wno-macro-redefined -Wno-unused-function -mcpu=apple-m4
DISPATCH_SUPPORT_SOURCES := $(TORIDRAW_ROOT)/toridraw_font.c $(TORIDRAW_ROOT)/../bmp/bmp.c

CPPFLAGS += -I$(TORIDRAW_ROOT)
CFLAGS ?= -O3 -std=c11 -Wall -Wextra
LDLIBS += -lm

HOST_OS := $(shell uname -s)
HOST_ARCH := $(shell uname -m)
APPLE_PROJECTION_ASM := $(TORIDRAW_ROOT)/graphics/projection16.aarch64.S
NATIVE_ASM_CPPFLAGS :=
NATIVE_ASM_SOURCES :=

ifeq ($(HOST_OS)-$(HOST_ARCH),Darwin-arm64)
ifneq ($(wildcard $(APPLE_PROJECTION_ASM)),)
NATIVE_ASM_CPPFLAGS += -DHAVE_TORIDRAW_PROJECTION16_APPLE_ASM=1 \
	-DTORIDRAW_APPLE_NEON_PROJECTION_ASM=1
NATIVE_ASM_SOURCES += $(APPLE_PROJECTION_ASM)
endif
endif

CPPFLAGS += $(NATIVE_ASM_CPPFLAGS)

PROJECTION_SOURCES := \
	$(TORIDRAW_ROOT)/graphics/projection16_simd.u.c \
	$(TORIDRAW_ROOT)/graphics/projection16_simd.neon.u.c \
	$(TORIDRAW_ROOT)/graphics/projection16_simd.scalar.u.c \
	$(TORIDRAW_ROOT)/graphics/projection_zdiv_simd.neon.u.c \
	$(TORIDRAW_ROOT)/graphics/projection.u.c \
	$(TORIDRAW_ROOT)/graphics/projection_ortho.u.c

.PHONY: all run check benchmark sanitize dispatch-benchmark baseline-dispatch-benchmark clean

all: $(TARGET)

$(TARGET): bench.c $(PROJECTION_SOURCES) $(TORIDRAW_ROOT)/graphics/shared_tables.c $(NATIVE_ASM_SOURCES)
	$(CC) $(CPPFLAGS) $(CFLAGS) bench.c $(TORIDRAW_ROOT)/graphics/shared_tables.c \
		$(NATIVE_ASM_SOURCES) $(LDLIBS) -o $@

$(SANITIZE_TARGET): bench.c $(PROJECTION_SOURCES) $(TORIDRAW_ROOT)/graphics/shared_tables.c $(NATIVE_ASM_SOURCES)
	$(CC) $(CPPFLAGS) -O1 -g -std=c11 -Wall -Wextra \
		-fsanitize=undefined -fno-sanitize-recover=all -fno-omit-frame-pointer \
		bench.c $(TORIDRAW_ROOT)/graphics/shared_tables.c $(NATIVE_ASM_SOURCES) $(LDLIBS) -o $@

run: $(TARGET)
	./$(TARGET)

check: $(TARGET)
	./$(TARGET) --acceptance-only

benchmark: $(TARGET)
	./$(TARGET) --benchmark-only

sanitize: $(SANITIZE_TARGET)
	./$(SANITIZE_TARGET) --acceptance-only

ifneq ($(NATIVE_ASM_SOURCES),)
$(DISPATCH_TARGET): dispatch_bench.c $(TORIDRAW_ROOT)/toridraw_unity.c \
		$(NATIVE_ASM_SOURCES) $(DISPATCH_SUPPORT_SOURCES)
	$(CC) -I$(TORIDRAW_ROOT) -I$(TORIDRAW_ROOT)/../bmp \
		-DTORIDRAW_APPLE_NEON_PROJECTION_ASM=1 \
		$(DISPATCH_CFLAGS) dispatch_bench.c $(NATIVE_ASM_SOURCES) \
		$(DISPATCH_SUPPORT_SOURCES) $(LDLIBS) -o $@

dispatch-benchmark: $(DISPATCH_TARGET)
	./$(DISPATCH_TARGET)
else
dispatch-benchmark:
	@echo "dispatch benchmark requires Apple arm64 and $(APPLE_PROJECTION_ASM)" >&2
	@false
endif

baseline-dispatch-benchmark:
	@test -n "$(BASELINE_TORIDRAW_ROOT)" || \
		(echo "set BASELINE_TORIDRAW_ROOT to a clean baseline's 3rd/toridraw" >&2; exit 2)
	$(CC) -I$(BASELINE_TORIDRAW_ROOT) -I$(BASELINE_TORIDRAW_ROOT)/../bmp \
		-DBENCH_PORTABLE_BASELINE=1 $(DISPATCH_CFLAGS) dispatch_bench.c \
		$(BASELINE_TORIDRAW_ROOT)/toridraw_font.c \
		$(BASELINE_TORIDRAW_ROOT)/../bmp/bmp.c $(LDLIBS) -o $(BASELINE_DISPATCH_TARGET)
	./$(BASELINE_DISPATCH_TARGET)

clean:
	rm -f $(TARGET) $(SANITIZE_TARGET) $(DISPATCH_TARGET) $(BASELINE_DISPATCH_TARGET)
	rm -rf bench_projection_neon_acceptance.dSYM \
		bench_projection_neon_acceptance_sanitize.dSYM \
		bench_projection_native_dispatch.dSYM \
		bench_projection_baseline_dispatch.dSYM
