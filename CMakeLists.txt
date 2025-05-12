cmake_minimum_required(VERSION 3.7)
project(RSClient
    LANGUAGES C
)

find_package(SDL2 REQUIRED)
include_directories(RSClient ${SDL2_INCLUDE_DIRS})

add_executable(rs
    src/main.c
    src/projection.c
    src/gouraud.c
    src/load_separate.c
    src/texture.c
    src/lighting.c
)

add_executable(texture_test
    src/texture_test.c
    src/texture.c
)

target_link_libraries(rs ${SDL2_LIBRARIES})
target_link_libraries(texture_test ${SDL2_LIBRARIES})