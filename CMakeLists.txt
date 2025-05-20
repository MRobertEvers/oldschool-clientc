cmake_minimum_required(VERSION 3.7)
project(RSClient
    LANGUAGES C
)

find_package(SDL2 REQUIRED)
find_package(BZip2 REQUIRED)
find_package(ZLIB REQUIRED)
include_directories(RSClient ${SDL2_INCLUDE_DIRS} ${BZIP2_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIRS})

add_executable(rs
    src/main.c
    src/projection.c
    src/gouraud.c
    src/load_separate.c
    src/texture.c
    src/lighting.c
    src/buffer.c
)

add_executable(osrs_cache_test
    src/osrs_cache_test.c
    src/osrs_cache2.c
    src/xtea.c
    src/buffer.c
    src/sequence.c
)

add_executable(texture_test
    src/texture_test.c
    src/texture.c
    src/buffer.c
)

target_link_libraries(rs ${SDL2_LIBRARIES})
target_link_libraries(texture_test ${SDL2_LIBRARIES})
target_link_libraries(osrs_cache_test ${SDL2_LIBRARIES} ${BZIP2_LIBRARIES} ${ZLIB_LIBRARIES})