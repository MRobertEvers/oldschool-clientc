# runescript-lsp — the language server for RuneScript and the content tree.
#
#   make -C tools/runescript-lsp              -> tools/runescript-lsp/runescript-lsp
#   make -C tools/runescript-lsp OPT=0        -> unoptimised, for a debugger
#   make -C tools/runescript-lsp test         -> the protocol smoke test
#   make -C tools/runescript-lsp clean
#
# On Windows use CMake instead (CMakeLists.txt beside this file) — the sources
# are portable, this Makefile is not:
#
#   cmake -S tools/runescript-lsp -B build-lsp && cmake --build build-lsp --config Release
#
# The generated parsers are checked in, so building needs no Node and no
# tree-sitter CLI. Regenerating them after a grammar edit does:
#
#   make -C tools/runescript-lsp grammars

ROOT := ../..
SRC_DIR := src
OBJ_DIR := build
BIN := runescript-lsp

OPT ?= 1
ifeq ($(OPT),1)
CFLAGS_OPT := -O2
else
CFLAGS_OPT := -O0 -g
endif

# Wall/Wextra, but not -Werror: the generated parsers are not ours to keep
# warning-free, and a grammar regeneration must not break the build.
# -MMD -MP writes a .d beside each .o listing the headers it read, and the
# include at the bottom feeds them back to make. Without it a header edit
# rebuilds nothing: `struct RS_Symbol` grew a field once and index.o was
# rebuilt while server.o was not, which is a silent struct-size mismatch and
# reads as a segfault a long way from the edit.
CFLAGS := -std=c11 $(CFLAGS_OPT) -MMD -MP -Wall -Wextra -Wno-unused-parameter \
          -I$(SRC_DIR) \
          -I$(ROOT)/3rd/tree-sitter/include \
          -I$(ROOT)/src \
          -I$(ROOT)/3rd/rscache/src

TS_RUNTIME := $(ROOT)/3rd/tree-sitter/src/lib.c
TS_SCRIPT := $(ROOT)/tools/tree-sitter-runescript/src/parser.c
TS_CONFIG := $(ROOT)/tools/tree-sitter-runeconfig/src/parser.c

# The two tables the engine already keeps: server opcode names and arity, and
# the client command table with its typed prototypes. Linking them is what
# makes hover and completion agree with the VM rather than with a copy of it.
ENGINE_SRC := $(ROOT)/src/serverscript/ss_meta.c \
              $(ROOT)/3rd/rscache/src/cs2/cs2_command.c \
              $(ROOT)/3rd/rscache/src/cs2/cs2_types.c \
              $(ROOT)/3rd/rscache/src/cs2/cs2_support.c

OWN_SRC := $(SRC_DIR)/server.c $(SRC_DIR)/index.c $(SRC_DIR)/doc.c \
           $(SRC_DIR)/json.c $(SRC_DIR)/util.c $(SRC_DIR)/platform.c

OBJS := $(OBJ_DIR)/lib.o $(OBJ_DIR)/parser_runescript.o $(OBJ_DIR)/parser_runeconfig.o \
        $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(OWN_SRC)) \
        $(OBJ_DIR)/ss_meta.o $(OBJ_DIR)/cs2_command.o $(OBJ_DIR)/cs2_types.o \
        $(OBJ_DIR)/cs2_support.o

.PHONY: all clean test grammars command-docs install

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# The tree-sitter runtime and the two generated parsers. Each names its own
# include root: a generated parser includes "tree_sitter/parser.h" from beside
# itself, which is not the same header as the runtime's public api.h.
$(OBJ_DIR)/lib.o: $(TS_RUNTIME) | $(OBJ_DIR)
	$(CC) -std=c11 $(CFLAGS_OPT) -MMD -MP -I$(ROOT)/3rd/tree-sitter/include \
	      -I$(ROOT)/3rd/tree-sitter/src -c $< -o $@

$(OBJ_DIR)/parser_runescript.o: $(TS_SCRIPT) | $(OBJ_DIR)
	$(CC) -std=c11 $(CFLAGS_OPT) -MMD -MP -I$(ROOT)/tools/tree-sitter-runescript/src -c $< -o $@

$(OBJ_DIR)/parser_runeconfig.o: $(TS_CONFIG) | $(OBJ_DIR)
	$(CC) -std=c11 $(CFLAGS_OPT) -MMD -MP -I$(ROOT)/tools/tree-sitter-runeconfig/src -c $< -o $@

$(OBJ_DIR)/ss_meta.o: $(ROOT)/src/serverscript/ss_meta.c | $(OBJ_DIR)
	$(CC) -std=c11 $(CFLAGS_OPT) -MMD -MP -I$(ROOT)/src -c $< -o $@

$(OBJ_DIR)/cs2_command.o: $(ROOT)/3rd/rscache/src/cs2/cs2_command.c | $(OBJ_DIR)
	$(CC) -std=c11 $(CFLAGS_OPT) -MMD -MP -I$(ROOT)/3rd/rscache/src -I$(ROOT)/3rd/rscache/include \
	      -c $< -o $@

$(OBJ_DIR)/cs2_types.o: $(ROOT)/3rd/rscache/src/cs2/cs2_types.c | $(OBJ_DIR)
	$(CC) -std=c11 $(CFLAGS_OPT) -MMD -MP -I$(ROOT)/3rd/rscache/src -I$(ROOT)/3rd/rscache/include \
	      -c $< -o $@

# cs2_types.c formats a script name through the library's arena allocator.
$(OBJ_DIR)/cs2_support.o: $(ROOT)/3rd/rscache/src/cs2/cs2_support.c | $(OBJ_DIR)
	$(CC) -std=c11 $(CFLAGS_OPT) -MMD -MP -I$(ROOT)/3rd/rscache/src -I$(ROOT)/3rd/rscache/include \
	      -c $< -o $@

# Regenerate the command signature/description table from the reference
# server's engine.rs2. Needs that tree; the output is checked in so that
# building does not.
#   make -C tools/runescript-lsp command-docs REF=../../../LostCity_Server
REF ?= $(ROOT)/../LostCity_Server
command-docs:
	python3 gen_command_docs.py $(REF)

# Regenerate both parsers from their grammars. Needs Node; the outputs are
# checked in so that building does not.
grammars:
	cd $(ROOT)/tools/tree-sitter-runescript && npx --yes tree-sitter-cli@0.26.12 generate
	cd $(ROOT)/tools/tree-sitter-runeconfig && npx --yes tree-sitter-cli@0.26.12 generate

test: $(BIN)
	python3 test/protocol_test.py

clean:
	rm -rf $(OBJ_DIR) $(BIN)

-include $(OBJS:.o=.d)
