# From scripts/painter/c: make && ./bench_painter

CC ?= cc
CFLAGS ?= -O3 -Wall -Wextra -Wno-unused-parameter
LDFLAGS ?= -lm
BENCH_ITERS ?= 100

SOURCES = bench.c painter_common.c painter_bucket.c painter_world3d.c

.PHONY: all clean run

all: bench_painter

bench_painter: $(SOURCES) painter_common.h Makefile
	$(CC) $(CFLAGS) -DBENCH_ITERS=$(BENCH_ITERS) $(SOURCES) $(LDFLAGS) -o $@

run: all
	./bench_painter

clean:
	rm -f bench_painter
