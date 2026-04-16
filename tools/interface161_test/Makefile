# Thin wrapper: build the CMake target from repo root.
ROOT := $(abspath ../../..)
BUILD ?= $(ROOT)/build

.PHONY: all clean

all:
	@mkdir -p "$(BUILD)"
	@cd "$(BUILD)" && cmake "$(ROOT)" && cmake --build . --target interface161_test

clean:
	rm -rf "$(BUILD)"
