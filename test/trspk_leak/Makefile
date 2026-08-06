# CPU-only fixed-function D3D9 retained-renderer leak regression.
#
# From a shell with the Win32 MinGW toolchain on PATH:
#   mingw32-make -C test/trspk_leak check CC=gcc

ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
BUILD_DIR := .build
TARGET := trspk_leak_test.exe

CC ?= gcc
CFLAGS ?= -std=gnu11 -O2 -g -Wall -Wextra
CPPFLAGS += -I$(ROOT)/src -I$(ROOT)/3rd/trspk -I$(ROOT)/3rd/toridraw \
	-I$(ROOT)/3rd/bmp
TEST_DEFINES := -DTORIRS_D3D9_RETAINED_TEST_API=1 \
	-DTORIRS_RETAINED_ALLOC_TRACKING=1
TRACK_ALLOC := -include $(CURDIR)/retained_alloc_tracker.h
SECTION_FLAGS := -ffunction-sections -fdata-sections
DEPFLAGS := -MMD -MP
LDFLAGS += -Wl,--gc-sections
LDLIBS += -ld3d9 -lgdi32 -luser32 -lkernel32 -lm

TRACKED_CFLAGS := $(CPPFLAGS) $(CFLAGS) $(SECTION_FLAGS) $(DEPFLAGS) \
	$(TEST_DEFINES) $(TRACK_ALLOC)
PLAIN_CFLAGS := $(CPPFLAGS) $(CFLAGS) $(SECTION_FLAGS) $(DEPFLAGS) \
	-DTORIRS_D3D9_RETAINED_TEST_API=1

OBJECTS := \
	$(BUILD_DIR)/trspk_leak_test.o \
	$(BUILD_DIR)/retained_alloc_tracker.o \
	$(BUILD_DIR)/retained_renderer_stubs.o \
	$(BUILD_DIR)/platform_win32_renderer_d3d9.o \
	$(BUILD_DIR)/trspk_toridraw.o \
	$(BUILD_DIR)/toridraw_unity.o \
	$(BUILD_DIR)/toridraw_font.o \
	$(BUILD_DIR)/bmp.o \
	$(BUILD_DIR)/trspk_unity.o

.PHONY: all clean check test

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS) $(LDLIBS)

$(BUILD_DIR):
	mkdir -p $@

$(BUILD_DIR)/trspk_leak_test.o: trspk_leak_test.c retained_alloc_tracker.h | $(BUILD_DIR)
	$(CC) $(TRACKED_CFLAGS) -c $< -o $@

$(BUILD_DIR)/retained_alloc_tracker.o: retained_alloc_tracker.c retained_alloc_tracker.h | $(BUILD_DIR)
	$(CC) $(PLAIN_CFLAGS) -c $< -o $@

$(BUILD_DIR)/retained_renderer_stubs.o: retained_renderer_stubs.c | $(BUILD_DIR)
	$(CC) $(TRACKED_CFLAGS) -c $< -o $@

$(BUILD_DIR)/platform_win32_renderer_d3d9.o: \
	$(ROOT)/src/platform/platform_win32_renderer_d3d9.c | $(BUILD_DIR)
	$(CC) $(TRACKED_CFLAGS) -c $< -o $@

$(BUILD_DIR)/trspk_toridraw.o: $(ROOT)/src/render/trspk_toridraw.c | $(BUILD_DIR)
	$(CC) $(TRACKED_CFLAGS) -c $< -o $@

$(BUILD_DIR)/toridraw_unity.o: $(ROOT)/3rd/toridraw/toridraw_unity.c | $(BUILD_DIR)
	$(CC) $(TRACKED_CFLAGS) -w -c $< -o $@

$(BUILD_DIR)/toridraw_font.o: $(ROOT)/3rd/toridraw/toridraw_font.c | $(BUILD_DIR)
	$(CC) $(TRACKED_CFLAGS) -w -c $< -o $@

$(BUILD_DIR)/bmp.o: $(ROOT)/3rd/bmp/bmp.c | $(BUILD_DIR)
	$(CC) $(TRACKED_CFLAGS) -c $< -o $@

$(BUILD_DIR)/trspk_unity.o: $(ROOT)/3rd/trspk/trspk_unity.c | $(BUILD_DIR)
	$(CC) $(TRACKED_CFLAGS) -w -c $< -o $@

# The Win32 linker closes the output before some virus scanners release their
# newly-created-file handle; a short settle avoids a flaky CreateProcess e=32.
check test: $(TARGET)
	@sleep 1
	./$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(OBJECTS:.o=.d)
