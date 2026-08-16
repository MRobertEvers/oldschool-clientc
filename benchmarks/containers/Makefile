# From this directory: make && ./bench_containers
# CSV: ./bench_containers --csv

CC ?= cc
CFLAGS ?= -O3 -std=c11 -Wall -Wextra
CPPFLAGS ?= -I. -I../../3rd/hmap
LDFLAGS ?= -lm

HMAP_SRC = ../../3rd/hmap/hmap.c
HMAP_OBJ = hmap.o

.PHONY: all clean run

all: bench_containers

$(HMAP_OBJ): $(HMAP_SRC) ../../3rd/hmap/hmap.h
	$(CC) $(CPPFLAGS) -O3 -std=c11 -w -c $(HMAP_SRC) -o $@

bench_containers: bench.c $(HMAP_OBJ) \
		bench_object.h bench_vec.h bench_list.h bench_rbtree.h Makefile
	$(CC) $(CPPFLAGS) $(CFLAGS) bench.c $(HMAP_OBJ) $(LDFLAGS) -o $@

run: all
	./bench_containers

clean:
	rm -f bench_containers $(HMAP_OBJ)
