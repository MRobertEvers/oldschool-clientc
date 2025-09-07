#!/bin/bash

# Script to dump assembly of specific functions using objdump
# Functions: raster_gouraud_s4 and gouraud_deob_draw_triangle

BUILD_DIR="build_release"
OUTPUT_DIR="assembly_dumps"

# Create output directory if it doesn't exist
mkdir -p "$OUTPUT_DIR"

# Color output for better readability
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== Assembly Dump Script ===${NC}"
echo -e "${YELLOW}Dumping assembly for:${NC}"
echo "  - raster_gouraud_s4"
echo "  - gouraud_deob_draw_triangle"
echo

# Function to dump assembly from object file
dump_from_object() {
    local func_name="$1"
    local obj_file="$2"
    local output_file="$3"
    
    if [ -f "$obj_file" ]; then
        echo -e "${GREEN}Dumping $func_name from object file: $obj_file${NC}"
        objdump -d --disassemble="$func_name" "$obj_file" > "$output_file"
        if [ $? -eq 0 ]; then
            echo -e "  ✓ Saved to: $output_file"
        else
            echo -e "${RED}  ✗ Failed to dump from object file${NC}"
        fi
    else
        echo -e "${RED}  ✗ Object file not found: $obj_file${NC}"
    fi
}

# Function to dump assembly from binary
dump_from_binary() {
    local func_name="$1"
    local binary="$2"
    local output_file="$3"
    
    if [ -f "$binary" ]; then
        echo -e "${GREEN}Dumping $func_name from binary: $binary${NC}"
        objdump -d --disassemble="$func_name" "$binary" > "$output_file"
        if [ $? -eq 0 ]; then
            echo -e "  ✓ Saved to: $output_file"
        else
            echo -e "${RED}  ✗ Failed to dump from binary${NC}"
        fi
    else
        echo -e "${RED}  ✗ Binary not found: $binary${NC}"
    fi
}

# Function to show assembly stats
show_stats() {
    local output_file="$1"
    if [ -f "$output_file" ]; then
        local lines=$(wc -l < "$output_file")
        local size=$(du -h "$output_file" | cut -f1)
        echo -e "    Lines: $lines, Size: $size"
    fi
}

echo -e "${YELLOW}=== Dumping raster_gouraud_s4 ===${NC}"

# From object files
dump_from_object "raster_gouraud_s4" \
    "$BUILD_DIR/CMakeFiles/main_client.dir/src/gouraud.c.o" \
    "$OUTPUT_DIR/raster_gouraud_s4_obj.asm"
show_stats "$OUTPUT_DIR/raster_gouraud_s4_obj.asm"

dump_from_object "raster_gouraud_s4" \
    "$BUILD_DIR/CMakeFiles/model_viewer.dir/src/gouraud.c.o" \
    "$OUTPUT_DIR/raster_gouraud_s4_model_viewer_obj.asm"
show_stats "$OUTPUT_DIR/raster_gouraud_s4_model_viewer_obj.asm"

# From binaries
dump_from_binary "raster_gouraud_s4" \
    "$BUILD_DIR/main_client" \
    "$OUTPUT_DIR/raster_gouraud_s4_main_client.asm"
show_stats "$OUTPUT_DIR/raster_gouraud_s4_main_client.asm"

dump_from_binary "raster_gouraud_s4" \
    "$BUILD_DIR/model_viewer" \
    "$OUTPUT_DIR/raster_gouraud_s4_model_viewer.asm"
show_stats "$OUTPUT_DIR/raster_gouraud_s4_model_viewer.asm"

echo
echo -e "${YELLOW}=== Dumping gouraud_deob_draw_triangle ===${NC}"

# From object files
dump_from_object "gouraud_deob_draw_triangle" \
    "$BUILD_DIR/CMakeFiles/main_client.dir/src/gouraud_deob.c.o" \
    "$OUTPUT_DIR/gouraud_deob_draw_triangle_obj.asm"
show_stats "$OUTPUT_DIR/gouraud_deob_draw_triangle_obj.asm"

dump_from_object "gouraud_deob_draw_triangle" \
    "$BUILD_DIR/CMakeFiles/model_viewer.dir/src/gouraud_deob.c.o" \
    "$OUTPUT_DIR/gouraud_deob_draw_triangle_model_viewer_obj.asm"
show_stats "$OUTPUT_DIR/gouraud_deob_draw_triangle_model_viewer_obj.asm"

# From binaries
dump_from_binary "gouraud_deob_draw_triangle" \
    "$BUILD_DIR/main_client" \
    "$OUTPUT_DIR/gouraud_deob_draw_triangle_main_client.asm"
show_stats "$OUTPUT_DIR/gouraud_deob_draw_triangle_main_client.asm"

dump_from_binary "gouraud_deob_draw_triangle" \
    "$BUILD_DIR/model_viewer" \
    "$OUTPUT_DIR/gouraud_deob_draw_triangle_model_viewer.asm"
show_stats "$OUTPUT_DIR/gouraud_deob_draw_triangle_model_viewer.asm"

echo
echo -e "${GREEN}=== Summary ===${NC}"
echo "Assembly dumps created in: $OUTPUT_DIR/"
ls -la "$OUTPUT_DIR/" 2>/dev/null || echo "No files created"

echo
echo -e "${YELLOW}=== Additional objdump options you can try ===${NC}"
echo "For more detailed disassembly with source code interleaving:"
echo "  objdump -S --disassemble=raster_gouraud_s4 $BUILD_DIR/main_client"
echo
echo "For Intel syntax instead of AT&T:"
echo "  objdump -d -M intel --disassemble=raster_gouraud_s4 $BUILD_DIR/main_client"
echo
echo "For symbol table information:"
echo "  objdump -t $BUILD_DIR/main_client | grep -E '(raster_gouraud_s4|gouraud_deob_draw_triangle)'"
