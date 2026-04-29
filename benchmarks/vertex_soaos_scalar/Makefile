# From benchmarks/vertex_soaos_scalar: make && ./bench_vertex_soaos_scalar
#
# Compares flat SoA (three float streams) vs AoSoA block traversal using the same
# TRSPK_VertexMetalSoaos layout as the engine (see metal_vertex.h).

CC ?= cc
CFLAGS ?= -O3 -std=c11 -Wall -Wextra

BENCH_VERTS ?= 4194304
BENCH_PASSES ?= 24
BENCH_WARMUP ?= 2

.PHONY: all clean run

all: bench_vertex_soaos_scalar

bench_vertex_soaos_scalar: bench.c Makefile
	$(CC) $(CFLAGS) \
		-DBENCH_VERTS=$(BENCH_VERTS) \
		-DBENCH_PASSES=$(BENCH_PASSES) \
		-DBENCH_WARMUP=$(BENCH_WARMUP) \
		bench.c -o $@

run: all
	./bench_vertex_soaos_scalar

clean:
	rm -f bench_vertex_soaos_scalar
