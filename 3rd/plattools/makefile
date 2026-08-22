# Standalone plattools library and test runner.
#
#   make -C 3rd/plattools
#   make -C 3rd/plattools test
#   make -C 3rd/plattools clean

CC ?= cc
AR ?= ar
CFLAGS ?= -std=c11 -O2
CFLAGS += -Wall -Wextra -Werror -MMD -MP

BUILD := build
OBJECT := $(BUILD)/plattools.o
LIBRARY := $(BUILD)/libplattools.a
TEST := $(BUILD)/plattools_test

.PHONY: all test clean
.DEFAULT_GOAL := all

all: $(LIBRARY)

$(BUILD):
	mkdir -p $(BUILD)

$(OBJECT): plattools.c plattools.h | $(BUILD)
	$(CC) $(CFLAGS) -c plattools.c -o $@

$(LIBRARY): $(OBJECT)
	$(AR) rcs $@ $^

$(TEST): plattools_test.c $(LIBRARY)
	$(CC) $(CFLAGS) plattools_test.c $(LIBRARY) -o $@

test: $(TEST)
	./$(TEST)

clean:
	rm -rf $(BUILD)

-include $(BUILD)/*.d
