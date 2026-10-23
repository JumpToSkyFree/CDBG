cmake_minimum_required(VERSION 3.21)

project(cdbg
    VERSION 0.1.0
    DESCRIPTION "A TUI-based debugger"
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(CDBG_BUILD_TESTS "Build the cdbg test suite" ON)
option(CDBG_ENABLE_ASAN "Enable AddressSanitizer/UBSan in Debug builds" ON)

if(CDBG_ENABLE_ASAN AND CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

# include(FetchContent)
include(cmake/Dependencies.cmake)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBDW REQUIRED IMPORTED_TARGET libdw)
pkg_check_modules(CAPSTONE REQUIRED IMPORTED_TARGET capstone)

add_library(cdbg_lib STATIC
#     src/engine/backend.cpp
#     src/engine/linux_backend.cpp
#     src/engine/breakpoint.cpp
#     src/engine/event.cpp
#     src/engine/symbols.cpp
#     src/engine/engine.cpp
#     src/ui/panel.cpp
#     src/ui/input.cpp
#     src/ui/command_tree.cpp
#     src/ui/loop.cpp
#     src/source_view/ts_parser.cpp
#     src/source_view/inlay.cpp
    include/engine/internal/symbols_linux_impl.hpp
    include/engine/internal/disassembler_linux_impl.hpp
    include/error.hpp
    src/engine/backend.cpp
    src/engine/breakpoint.cpp
    src/engine/breakpoint_list.cpp
    src/engine/disassembler.cpp
    src/engine/process.cpp
    src/engine/symbols.cpp
    src/tui/debugger.cpp
    src/engine/source_loader.cpp
    src/tui/source_view.cpp
)

target_include_directories(cdbg_lib
     PUBLIC
         ${CMAKE_CURRENT_SOURCE_DIR}/include
     PRIVATE
         ${CMAKE_CURRENT_SOURCE_DIR}/src
       )

target_link_libraries(cdbg_lib
    PUBLIC
        util
        ftxui::screen
        ftxui::dom
        ftxui::component
        PkgConfig::LIBDW
        PkgConfig::CAPSTONE
        tree-sitter
        tree-sitter-c
        tree-sitter-cpp
        fmt::fmt
)

target_compile_features(cdbg_lib PUBLIC cxx_std_20)

add_executable(cdbg src/main.cpp)

target_link_libraries(cdbg
    PRIVATE
    cdbg_lib
    popl::popl
)

if(CDBG_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
