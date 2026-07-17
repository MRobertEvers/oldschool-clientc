# Build gen_painters_cullmap from this directory:
#   make
#   ./gen_painters_cullmap --radius 25 --near 512 --width 1920 --height 1080 -o out.c
#   ./gen_painters_cullmap --radius 25 --near 512 --width 1920 --height 1080 --blob -o out.bin
#   node batch_cullmaps.mjs
#   node batch_cullmaps.mjs -j8

REPO := $(abspath ../..)
SRC_DIR := $(REPO)/src
TORIDRAW_DIR := $(REPO)/3rd/toridraw

CC ?= cc
CFLAGS += -Wall -Wextra -O2 \
	-I$(SRC_DIR) \
	-I$(SRC_DIR)/painters \
	-I$(TORIDRAW_DIR)

SRCS := main.c \
	$(SRC_DIR)/painters/painters.c \
	$(SRC_DIR)/painters/painters_cull_project.c \
	$(TORIDRAW_DIR)/graphics/shared_tables.c

# libm for sin/cos/tan in shared_tables (not needed on typical Windows link lines)
LIBS :=
ifeq ($(OS),Windows_NT)
else
LIBS += -lm
endif

.PHONY: all clean

all: gen_painters_cullmap

gen_painters_cullmap: $(SRCS)
	$(CC) $(CFLAGS) -o $@ $(SRCS) $(LIBS)

clean:
	rm -f gen_painters_cullmap
