# Build gen_painters_cullmap from this directory:
#   make
#   ./gen_painters_cullmap --radius 25 --near 512 --width 1920 --height 1080 -o out.c

REPO := $(abspath ../..)

CC ?= cc
CFLAGS += -Wall -Wextra -O2 -I$(REPO)/src

SRCS := main.c $(REPO)/src/graphics/shared_tables.c

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
