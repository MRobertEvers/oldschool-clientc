ROOT := $(abspath ../..)
BUILD ?= $(ROOT)/build

.PHONY: all clean

all:
	@mkdir -p "$(BUILD)"
	@cd "$(BUILD)" && cmake "$(ROOT)" && cmake --build . --target dump_interface_index

clean:
	rm -f "$(BUILD)/dump_interface_index"
