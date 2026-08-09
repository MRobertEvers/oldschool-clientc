# Standalone build against 3rd/rscache (not the v0 unity in CMake).
CC      ?= cc
REPO    := $(abspath ../..)
RSCACHE := $(REPO)/3rd/rscache
THIRD   := $(REPO)/3rd
TARGET  := dump_stats

INCLUDES := -I$(RSCACHE)/include -I$(RSCACHE)/src -I$(RSCACHE) \
	-I$(THIRD)/bzip -I$(THIRD)/miniz -I$(THIRD)/xteas -I$(THIRD)/windows_cp1252

# -O2, not -O0: this walks every npc and obj record in the cache, and the
# vendored deps below are built optimised anyway.
CFLAGS  ?= -std=c11 -g -O2 -Wall -Wextra -Wno-unused-parameter -Wno-pointer-sign
CFLAGS  += $(INCLUDES)
DEP_CFLAGS := -std=c11 -O3 -g -w $(INCLUDES)

BUILD := build

.PHONY: all clean

all: $(TARGET)

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/rscache_unity.o: $(RSCACHE)/rscache_unity.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/bzip.o: $(THIRD)/bzip/bzip.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/bzip_encode.o: $(THIRD)/bzip/bzip_encode.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/miniz.o: $(THIRD)/miniz/miniz.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/xteas.o: $(THIRD)/xteas/xteas.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/windows_cp1252.o: $(THIRD)/windows_cp1252/windows_cp1252.c | $(BUILD)
	$(CC) $(DEP_CFLAGS) -c $< -o $@

$(BUILD)/main.o: main.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(TARGET): $(BUILD)/main.o $(BUILD)/rscache_unity.o $(BUILD)/bzip.o \
		$(BUILD)/bzip_encode.o $(BUILD)/miniz.o $(BUILD)/xteas.o \
		$(BUILD)/windows_cp1252.o
	$(CC) $(CFLAGS) -o $@ $^ -lm

clean:
	rm -rf $(BUILD) $(TARGET)
