ROOT := $(abspath ../..)
BUILD ?= $(ROOT)/build

.PHONY: all clean

all:
	@mkdir -p "$(BUILD)"
	@cd "$(BUILD)" && cmake "$(ROOT)" && cmake --build . --target dump_map_index

clean:
	rm -f "$(BUILD)/dump_map_index"
