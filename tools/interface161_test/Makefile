CC = clang
OBJ_DIR = build
TARGET = interface161_test

REPO_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
SRC_DIR   := $(REPO_ROOT)/src
SRC2_DIR  := $(REPO_ROOT)/src2
TOOL_DIR  := $(REPO_ROOT)/tools/interface161_test

CFLAGS = -std=c11 -Wall -Wextra -g \
	-I$(TOOL_DIR) -I$(SRC2_DIR)/toridraw -I$(SRC2_DIR)/toriauxlib/td -I$(SRC2_DIR) -I$(SRC_DIR)

LDFLAGS = -lm

ifdef ASAN
CFLAGS += -fsanitize=address -fno-omit-frame-pointer
LDFLAGS += -fsanitize=address
endif

SRCS = $(TOOL_DIR)/main.c \
	$(TOOL_DIR)/fixture.c \
	$(TOOL_DIR)/cs2_runner.c \
	$(TOOL_DIR)/enum_lookup.c \
	$(TOOL_DIR)/struct_lookup.c \
	$(TOOL_DIR)/stubs.c \
	$(SRC_DIR)/bmp.c \
	$(SRC_DIR)/osrs/rscache/unity.c \
	$(SRC2_DIR)/toridraw/toridraw_unity.c \
	$(SRC2_DIR)/toridraw/toridraw_model_sprite.c \
	$(SRC2_DIR)/toridraw/toridraw_font.c \
	$(SRC2_DIR)/toriauxlib/td/toridraw_cachemodel.c \
	$(SRC2_DIR)/toriauxlib/c/toriauxlibcache_clientscript_convert.c \
	$(SRC2_DIR)/ui/uitree.c \
	$(SRC2_DIR)/ui/uitree_layout.c \
	$(SRC2_DIR)/ui/rs_inv_container.c \
	$(SRC2_DIR)/ui/ui_inv_data_service.c \
	$(SRC2_DIR)/vm/cs1vm.c \
	$(SRC2_DIR)/vm/cs2vm.c \
	$(SRC2_DIR)/vm/cs2_opcode_meta.c \
	$(SRC2_DIR)/vm/cs2_script.c \
	$(SRC2_DIR)/vm/cs2_trigger_args.c \
	$(SRC2_DIR)/vm/cs2_host_ui.c \
	$(SRC2_DIR)/games/ie_enum_lookup.c \
	$(SRC2_DIR)/games/ie_struct_lookup.c \
	$(SRC2_DIR)/games/ie_param_lookup.c \
	$(SRC_DIR)/3rd/bzip/bzip.c \
	$(SRC_DIR)/3rd/miniz/miniz.c

OBJS = $(addprefix $(OBJ_DIR)/, $(notdir $(SRCS:.c=.o)))

all: $(OBJ_DIR) $(TARGET)

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(OBJ_DIR)/main.o: $(TOOL_DIR)/main.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/fixture.o: $(TOOL_DIR)/fixture.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cs2_runner.o: $(TOOL_DIR)/cs2_runner.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/enum_lookup.o: $(TOOL_DIR)/enum_lookup.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/struct_lookup.o: $(TOOL_DIR)/struct_lookup.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/stubs.o: $(TOOL_DIR)/stubs.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/bmp.o: $(SRC_DIR)/bmp.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/unity.o: $(SRC_DIR)/osrs/rscache/unity.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toridraw_unity.o: $(SRC2_DIR)/toridraw/toridraw_unity.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toridraw_model_sprite.o: $(SRC2_DIR)/toridraw/toridraw_model_sprite.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toridraw_font.o: $(SRC2_DIR)/toridraw/toridraw_font.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toridraw_cachemodel.o: $(SRC2_DIR)/toriauxlib/td/toridraw_cachemodel.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/toriauxlibcache_clientscript_convert.o: $(SRC2_DIR)/toriauxlib/c/toriauxlibcache_clientscript_convert.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/uitree.o: $(SRC2_DIR)/ui/uitree.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/uitree_layout.o: $(SRC2_DIR)/ui/uitree_layout.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/rs_inv_container.o: $(SRC2_DIR)/ui/rs_inv_container.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/ui_inv_data_service.o: $(SRC2_DIR)/ui/ui_inv_data_service.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cs1vm.o: $(SRC2_DIR)/vm/cs1vm.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cs2vm.o: $(SRC2_DIR)/vm/cs2vm.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cs2_opcode_meta.o: $(SRC2_DIR)/vm/cs2_opcode_meta.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cs2_script.o: $(SRC2_DIR)/vm/cs2_script.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cs2_trigger_args.o: $(SRC2_DIR)/vm/cs2_trigger_args.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/cs2_host_ui.o: $(SRC2_DIR)/vm/cs2_host_ui.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/ie_enum_lookup.o: $(SRC2_DIR)/games/ie_enum_lookup.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/ie_struct_lookup.o: $(SRC2_DIR)/games/ie_struct_lookup.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/ie_param_lookup.o: $(SRC2_DIR)/games/ie_param_lookup.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/bzip.o: $(SRC_DIR)/3rd/bzip/bzip.c
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/miniz.o: $(SRC_DIR)/3rd/miniz/miniz.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(OBJ_DIR) $(TARGET)

.PHONY: all clean
