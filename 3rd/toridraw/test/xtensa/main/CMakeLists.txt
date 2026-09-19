# The test IS toridraw_presorted_xtensa_test.c, up in the library root beside
# every other proof; this component only names it and the kernel it scores.
get_filename_component(TORIDRAW_DIR "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

# PIXFMT: 6 = RGB565, 8 = RGB565_BE. The .S takes the matching build flag, and
# the header picks its entry points off the same fact.
if(NOT DEFINED PIXFMT)
    set(PIXFMT 6)
endif()

set(XTENSA_DEFS "")
if(PIXFMT EQUAL 8)
    set(XTENSA_DEFS "-DTORIDRAW_XTENSA_RGB565_BE=1")
endif()

idf_component_register(
    SRCS "${TORIDRAW_DIR}/toridraw_presorted_xtensa_test.c" "${TORIDRAW_DIR}/impl/raster/asm/tri.flat.rgb565.xtensa.S"
    INCLUDE_DIRS "${TORIDRAW_DIR}")

target_compile_options(${COMPONENT_LIB} PRIVATE
    -DTORIDRAW_PIXEL_FORMAT=${PIXFMT} ${XTENSA_DEFS}
    -Wno-unused-function -Wno-unused-variable -O2)

set_source_files_properties("${TORIDRAW_DIR}/impl/raster/asm/tri.flat.rgb565.xtensa.S"
    PROPERTIES COMPILE_OPTIONS "-mtext-section-literals;${XTENSA_DEFS}")
