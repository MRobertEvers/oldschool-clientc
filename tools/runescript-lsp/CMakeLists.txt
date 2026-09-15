# Portable build for runescript-lsp — the lane Windows uses, and a working
# alternative to the Makefile on macOS and Linux.
#
#   cmake -S tools/runescript-lsp -B build-lsp
#   cmake --build build-lsp --config Release
#
# The binary lands in build-lsp/ (build-lsp/Release/ with a multi-config
# generator such as Visual Studio). The VS Code extension finds it by asking
# for `runescript.serverPath`, or by looking in the usual build directories.

cmake_minimum_required(VERSION 3.15)
project(runescript-lsp C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

get_filename_component(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

# --- the tree-sitter runtime and the two generated parsers ------------------
#
# Each is compiled on its own because their include roots differ: a generated
# parser includes "tree_sitter/parser.h" from its own src/, which is a
# different header from the runtime's public api.h.

add_library(tree_sitter_runtime STATIC "${REPO_ROOT}/3rd/tree-sitter/src/lib.c")
target_include_directories(tree_sitter_runtime PRIVATE
    "${REPO_ROOT}/3rd/tree-sitter/include"
    "${REPO_ROOT}/3rd/tree-sitter/src")
target_include_directories(tree_sitter_runtime INTERFACE
    "${REPO_ROOT}/3rd/tree-sitter/include")

add_library(tree_sitter_runescript STATIC
    "${REPO_ROOT}/tools/tree-sitter-runescript/src/parser.c")
target_include_directories(tree_sitter_runescript PRIVATE
    "${REPO_ROOT}/tools/tree-sitter-runescript/src")

add_library(tree_sitter_runeconfig STATIC
    "${REPO_ROOT}/tools/tree-sitter-runeconfig/src/parser.c")
target_include_directories(tree_sitter_runeconfig PRIVATE
    "${REPO_ROOT}/tools/tree-sitter-runeconfig/src")

# --- the engine's own name tables -------------------------------------------

add_library(runescript_engine_tables STATIC
    "${REPO_ROOT}/src/serverscript/ss_meta.c"
    "${REPO_ROOT}/3rd/rscache/src/cs2/cs2_command.c"
    "${REPO_ROOT}/3rd/rscache/src/cs2/cs2_types.c"
    "${REPO_ROOT}/3rd/rscache/src/cs2/cs2_support.c")
target_include_directories(runescript_engine_tables PUBLIC
    "${REPO_ROOT}/src"
    "${REPO_ROOT}/3rd/rscache/src"
    "${REPO_ROOT}/3rd/rscache/include")

# --- the server -------------------------------------------------------------

add_executable(runescript-lsp
    src/server.c
    src/index.c
    src/doc.c
    src/json.c
    src/util.c
    src/platform.c)

target_include_directories(runescript-lsp PRIVATE src)
target_link_libraries(runescript-lsp PRIVATE
    tree_sitter_runtime
    tree_sitter_runescript
    tree_sitter_runeconfig
    runescript_engine_tables)

if(MSVC)
    # The C runtime's "safe" replacements are not a portability story we can
    # follow, and this code checks its own bounds.
    target_compile_definitions(runescript-lsp PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_definitions(runescript_engine_tables PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(runescript-lsp PRIVATE /W3)
else()
    target_compile_options(runescript-lsp PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

install(TARGETS runescript-lsp RUNTIME DESTINATION bin)
