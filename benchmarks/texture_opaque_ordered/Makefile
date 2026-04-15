# From this directory: make && ./bench_texture_opaque_ordered

CC ?= cc
CFLAGS ?= -O3 -Wall -Wno-unused-function -Wno-unused-variable
CPPFLAGS ?= -I../../src/graphics
LDFLAGS ?= -lm

BENCH_ITERS ?= 500

.PHONY: all clean run

all: bench_texture_opaque_ordered

bench_texture_opaque_ordered: bench_texture_opaque_ordered.c Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) -DBENCH_ITERS=$(BENCH_ITERS) bench_texture_opaque_ordered.c $(LDFLAGS) -o $@

run: all
	./bench_texture_opaque_ordered

clean:
	rm -f bench_texture_opaque_ordered
