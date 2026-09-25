# Standalone build against 3rd/rscache, plus the client's sprite bridge.
#
# The bridge (engine/torirs_sprite_from_rscache.c) is compiled in rather than
# reimplemented: it is where the dat1 pix8/pix32 and dat2 pack decodes are
# turned into ARGB frames, and a second copy here would be a second thing to
# keep in step with the era gates.
CC      ?= cc
REPO    := $(abspath ../..)
RSCACHE := $(REPO)/3rd/rscache
THIRD   := $(REPO)/3rd
SRC     := $(REPO)/src
TARGET  := dump_sprites

INCLUDES := -I$(RSCACHE)/include -I$(RSCACHE)/src -I$(RSCACHE) \
	-I$(SRC) -I$(THIRD)/bzip -I$(THIRD)/miniz -I$(THIRD)/xteas

CFLAGS  ?= -std=c11 -g -O0 -Wall -Wextra -Wno-unused-parameter -Wno-pointer-sign
CFLAGS  += $(INCLUDES)
DEP_CFLAGS := -std=c11 -O3 -g -w $(INCLUDES)

BUILD := build

.PHONY: all clean

all: $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/rscache_unity.o: $(RSCACHE)/rscache_unity.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/bzip.o: $(THIRD)/bzip/bzip.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/bzip_encode.o: $(THIRD)/bzip/bzip_encode.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/miniz.o: $(THIRD)/miniz/miniz.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/xteas.o: $(THIRD)/xteas/xteas.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/sprite_from_rscache.o: $(SRC)/engine/torirs_sprite_from_rscache.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/torirs_types.o: $(SRC)/engine/torirs_types.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/component_hook.o: $(SRC)/engine/torirs_component_hook.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/main.o: main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(BUILD)/main.o $(BUILD)/sprite_from_rscache.o $(BUILD)/torirs_types.o $(BUILD)/component_hook.o $(BUILD)/rscache_unity.o \
		$(BUILD)/bzip.o $(BUILD)/bzip_encode.o $(BUILD)/miniz.o $(BUILD)/xteas.o
	$(CC) $(CFLAGS) -o $@ $^ -lm

clean:
	rm -rf $(BUILD) $(TARGET)
