ROOT := $(abspath ../..)
BUILD ?= $(ROOT)/build

.PHONY: all clean

all:
	@mkdir -p "$(BUILD)"
	@cd "$(BUILD)" && cmake "$(ROOT)" && cmake --build . --target match_dat2_interface

clean:
	rm -f "$(BUILD)/match_dat2_interface"
