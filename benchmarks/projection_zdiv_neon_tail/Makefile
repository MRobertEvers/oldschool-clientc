# From this directory: make && ./bench_projection_zdiv_neon_tail

CC ?= cc
CFLAGS ?= -O3 -std=c11 -Wall -Wextra

BENCH_ITERS ?= 2500000
BENCH_WARMUP ?= 8000

.PHONY: all clean run

all: bench_projection_zdiv_neon_tail

bench_projection_zdiv_neon_tail: bench.c Makefile
	$(CC) $(CFLAGS) -DBENCH_ITERS=$(BENCH_ITERS) -DBENCH_WARMUP=$(BENCH_WARMUP) bench.c -o $@

run: all
	./bench_projection_zdiv_neon_tail

clean:
	rm -f bench_projection_zdiv_neon_tail
