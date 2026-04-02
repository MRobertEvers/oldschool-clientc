# From this directory: make && ./bench_texture_scanline

CC ?= cc
CFLAGS ?= -O3 -Wall -Wno-unused-function -Wno-unused-variable
CPPFLAGS ?= -I../../src/graphics
LDFLAGS ?= -lm

BENCH_ITERS ?= 5000

.PHONY: all clean run

all: bench_texture_scanline

bench_texture_scanline: bench.c Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBENCH_ITERS=$(BENCH_ITERS) bench.c $(LDFLAGS) -o $@

run: all
	./bench_texture_scanline

clean:
	rm -f bench_texture_scanline
