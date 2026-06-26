# From scripts/painter/c: make && ./bench_painter
# Real painter fuzzer: make fuzz_real && ./fuzz_real

REPO_ROOT := ../../..
SRC_DIR   := $(REPO_ROOT)/src

CC ?= cc
CFLAGS ?= -O3 -Wall -Wextra -Wno-unused-parameter
LDFLAGS ?= -lm
BENCH_ITERS ?= 100

CPPFLAGS += -I$(SRC_DIR) -I$(SRC_DIR)/osrs -I$(SRC_DIR)/graphics

SOURCES = bench.c painter_common.c painter_bucket.c painter_world3d.c
FUZZ_SOURCES = fuzz_real.c $(SRC_DIR)/osrs/painters.c $(SRC_DIR)/graphics/shared_tables.c

.PHONY: all clean run fuzz fuzz_real bench_real

all: bench_painter

bench_painter: $(SOURCES) painter_common.h Makefile
	$(CC) $(CFLAGS) -DBENCH_ITERS=$(BENCH_ITERS) $(SOURCES) $(LDFLAGS) -o $@

fuzz_real: $(FUZZ_SOURCES) Makefile
	$(CC) $(CFLAGS) $(CPPFLAGS) $(FUZZ_SOURCES) $(LDFLAGS) -o $@

run: all
	./bench_painter

fuzz: fuzz_real
	./fuzz_real

# Same-seed performance benchmark against the real painters.
# Usage: make bench_real && ./fuzz_real 1 200 bench 200
bench_real: fuzz_real

clean:
	rm -f bench_painter fuzz_real
