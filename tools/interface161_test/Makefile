CC = clang
OBJ_DIR = build
TARGET = interface161_test

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SRC_DIR   := $(REPO_ROOT)/src
SRC2_DIR  := $(REPO_ROOT)/src2

CFLAGS = -std=c11 -Wall -Wextra -g \
	-I$(SRC2_DIR)/toridraw -I$(SRC2_DIR)/toriauxlib/td -I$(SRC2_DIR) -I$(SRC_DIR)

LDFLAGS = -lm

ifdef ASAN
CFLAGS += -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
endif

ifdef UBSAN
CFLAGS += -fsanitize=undefined -fno-omit-frame-pointer
LDFLAGS += -fsanitize=undefined
endif

SRCS = main.c \
	fixture.c \
	$(SRC_DIR)/bmp.c \
	$(SRC_DIR)/osrs/rscache/unity.c \
	$(SRC2_DIR)/toridraw/toridraw_unity.c \
	$(SRC2_DIR)/toridraw/toridraw_model_sprite.c \
	$(SRC2_DIR)/toridraw/toridraw_font.c \
	$(SRC2_DIR)/toriauxlib/td/toridraw_cachemodel.c \
	$(SRC_DIR)/3rd/bzip/bzip.c \
	$(SRC_DIR)/3rd/miniz/miniz.c

OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.c=.o)))

all: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

define make-goal
$(OBJ_DIR)/$(notdir $(1:.c=.o)): $(1)
	$(CC) $(CFLAGS) -c $(1) -o $$@
endef

$(foreach src,$(SRCS),$(eval $(call make-goal,$(src))))

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

asan:
	$(MAKE) clean
	$(MAKE) ASAN=1
	@printf '\nNote: Xcode clang AddressSanitizer may hang during startup on macOS 26+ (dyld init, before main).\n'
	@printf 'If ./interface161_test never prints anything, use: make run-debug\n'
	@printf 'Or build/run the ASAN binary on Linux.\n\n'

ubsan:
	$(MAKE) clean
	$(MAKE) UBSAN=1

run-debug: $(TARGET)
	MallocGuardEdges=1 MallocScribble=1 ./$(TARGET) $(ARGS)

.PHONY: all clean asan ubsan run-debug
