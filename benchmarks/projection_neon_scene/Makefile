# AArch64 only.  From this directory:
#   make run

CC ?= cc
CFLAGS ?= -O3 -std=c11 -Wall -Wextra

ROOT := ../..
RSCACHE := $(ROOT)/3rd/rscache
TORIDRAW := $(ROOT)/3rd/toridraw
ENGINE_OBJ_DIR ?= $(ROOT)/src/build_opt
ENGINE_OBJS := \
	$(ENGINE_OBJ_DIR)/rscache_unity.o \
	$(ENGINE_OBJ_DIR)/bzip.o \
	$(ENGINE_OBJ_DIR)/bzip_encode.o \
	$(ENGINE_OBJ_DIR)/miniz.o \
	$(ENGINE_OBJ_DIR)/xteas.o

INCLUDES := -I. -I$(ROOT) -I$(RSCACHE)/include -I$(RSCACHE)/src -I$(TORIDRAW)
TARGET := bench_projection_neon_scene

.PHONY: all clean run engine-objs

all: $(TARGET)

engine-objs:
	$(MAKE) -C $(ROOT)/src OPT=1 \
		build_opt/rscache_unity.o build_opt/bzip.o build_opt/bzip_encode.o \
		build_opt/miniz.o build_opt/xteas.o

$(TARGET): bench.c projection_neon_scene.h project_vertices_aarch64.S Makefile | engine-objs
	$(CC) $(CFLAGS) $(INCLUDES) \
		bench.c project_vertices_aarch64.S $(TORIDRAW)/graphics/shared_tables.c \
		$(ENGINE_OBJS) -lm -o $@

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET)
