# FindSDL2.cmake
# Locate SDL2 library
# This module defines:
# SDL2_FOUND - System has SDL2
# SDL2_INCLUDE_DIRS - SDL2 include directories
# SDL2_LIBRARIES - Libraries needed to use SDL2
# SDL2::SDL2 - Imported target for SDL2

# For MinGW, look in common locations
if(WIN32)
    # Check MinGW installation
    if(DEFINED ENV{MINGW_ROOT})
        set(SDL2_SEARCH_PATHS
            "$ENV{MINGW_ROOT}/mingw32"
            "$ENV{MINGW_ROOT}"
        )
    endif()
    
    # Also check CMAKE_PREFIX_PATH
    if(CMAKE_PREFIX_PATH)
        list(APPEND SDL2_SEARCH_PATHS ${CMAKE_PREFIX_PATH})
    endif()
    
    # Check for SDL2 in the MinGW compiler's directory
    if(CMAKE_C_COMPILER)
        get_filename_component(COMPILER_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
        get_filename_component(MINGW_PREFIX "${COMPILER_DIR}/.." ABSOLUTE)
        list(APPEND SDL2_SEARCH_PATHS "${MINGW_PREFIX}")
    endif()
    
    # Also check project's lib directory
    list(APPEND SDL2_SEARCH_PATHS "${CMAKE_SOURCE_DIR}/lib")
    
    # Find SDL2 include directory
    find_path(SDL2_INCLUDE_DIR
        NAMES SDL.h
        PATH_SUFFIXES include/SDL2 include SDL2
        PATHS ${SDL2_SEARCH_PATHS}
        NO_DEFAULT_PATH
    )
    
    # Also check system paths as fallback
    find_path(SDL2_INCLUDE_DIR
        NAMES SDL.h
        PATH_SUFFIXES include/SDL2 include SDL2
    )
    
    # Find SDL2 library
    find_library(SDL2_LIBRARY
        NAMES SDL2 SDL2main
        PATH_SUFFIXES lib lib/x86 lib/i686
        PATHS ${SDL2_SEARCH_PATHS}
        NO_DEFAULT_PATH
    )
    
    # Also check system paths as fallback
    find_library(SDL2_LIBRARY
        NAMES SDL2 SDL2main
        PATH_SUFFIXES lib lib/x86 lib/i686
    )
    
    # Find SDL2main library (needed for Windows)
    find_library(SDL2_MAIN_LIBRARY
        NAMES SDL2main
        PATH_SUFFIXES lib lib/x86 lib/i686
        PATHS ${SDL2_SEARCH_PATHS}
        NO_DEFAULT_PATH
    )
    
    # Also check system paths as fallback
    find_library(SDL2_MAIN_LIBRARY
        NAMES SDL2main
        PATH_SUFFIXES lib lib/x86 lib/i686
    )
    
    # Set libraries
    if(SDL2_LIBRARY)
        set(SDL2_LIBRARIES ${SDL2_LIBRARY})
        if(SDL2_MAIN_LIBRARY)
            list(APPEND SDL2_LIBRARIES ${SDL2_MAIN_LIBRARY})
        endif()
        # Add required Windows libraries
        list(APPEND SDL2_LIBRARIES mingw32 winmm imm32 version setupapi)
    endif()
else()
    # Unix/Linux SDL2 detection
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(SDL2 QUIET sdl2)
    endif()
    
    if(NOT SDL2_FOUND)
        find_path(SDL2_INCLUDE_DIR
            NAMES SDL.h
            PATH_SUFFIXES include/SDL2 SDL2
        )
        
        find_library(SDL2_LIBRARY
            NAMES SDL2
        )
        
        if(SDL2_INCLUDE_DIR AND SDL2_LIBRARY)
            set(SDL2_LIBRARIES ${SDL2_LIBRARY})
        endif()
    endif()
endif()

# Set include directories
if(SDL2_INCLUDE_DIR)
    set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR})
endif()

# Handle standard arguments
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(SDL2
    REQUIRED_VARS SDL2_LIBRARIES SDL2_INCLUDE_DIRS
    FAIL_MESSAGE "SDL2 not found. Please install SDL2 development files. See BUILD_WINXP.md for instructions."
)

# Debug output
if(NOT SDL2_FOUND)
    message(STATUS "SDL2_INCLUDE_DIR: ${SDL2_INCLUDE_DIR}")
    message(STATUS "SDL2_LIBRARY: ${SDL2_LIBRARY}")
    message(STATUS "SDL2_SEARCH_PATHS: ${SDL2_SEARCH_PATHS}")
endif()

# Create imported target
if(SDL2_FOUND AND NOT TARGET SDL2::SDL2)
    add_library(SDL2::SDL2 INTERFACE IMPORTED)
    set_target_properties(SDL2::SDL2 PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${SDL2_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${SDL2_LIBRARIES}"
    )
endif()

mark_as_advanced(SDL2_INCLUDE_DIR SDL2_LIBRARY SDL2_MAIN_LIBRARY)

