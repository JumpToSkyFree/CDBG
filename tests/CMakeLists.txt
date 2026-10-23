find_package(Catch2 REQUIRED)
 
add_executable(cdbg_tests
    test_breakpoint.cpp
    test_breakpoint_list.cpp
    test_backend.cpp
    test_symbols.cpp
    test_disassembler.cpp
    test_source_loader.cpp
    test_step.cpp
    test_line_window.cpp
    catch_main.cpp
)

target_include_directories(cdbg_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)
 
add_executable(simple_program simple_program.cpp)
add_dependencies(cdbg_tests simple_program)

# Branch/loop/call-heavy program for manual debugger sessions.  It is not used
# by the unit tests, but is built alongside them so `ninja` always produces it.
add_executable(complex_program complex_program.cpp)
add_dependencies(cdbg_tests complex_program)

target_compile_options(simple_program  PRIVATE -fno-sanitize=all)
target_link_options   (simple_program  PRIVATE -fno-sanitize=all)
target_compile_options(complex_program PRIVATE -fno-sanitize=all)
target_link_options   (complex_program PRIVATE -fno-sanitize=all)

if(TARGET Catch2::Catch2WithMain)
    target_link_libraries(cdbg_tests PRIVATE cdbg_lib Catch2::Catch2WithMain)
else()
    target_sources(cdbg_tests PRIVATE catch_main.cpp)
    target_link_libraries(cdbg_tests PRIVATE cdbg_lib Catch2::Catch2)
endif()

include(CTest)
if(Catch2_VERSION VERSION_GREATER_EQUAL 3)
    include(Catch)
else()
    include(ParseAndAddCatchTests)
endif()

set(CDBG_TEST_ENVIRONMENT
    "SIMPLE_PROGRAM_PATH=$<TARGET_FILE:simple_program>;COMPLEX_PROGRAM_PATH=$<TARGET_FILE:complex_program>;ASAN_OPTIONS=detect_leaks=0"
)

if(COMMAND catch_discover_tests)
  catch_discover_tests(cdbg_tests PROPERTIES ENVIRONMENT "${CDBG_TEST_ENVIRONMENT}")
elseif(COMMAND ParseAndAddCatchTests)
    ParseAndAddCatchTests(cdbg_tests)
    get_target_property(cdbg_test_names cdbg_tests ParseAndAddCatchTests_TESTS)
    if(cdbg_test_names)
        set_tests_properties(${cdbg_test_names} PROPERTIES
            ENVIRONMENT "${CDBG_TEST_ENVIRONMENT}"
        )
    endif()
endif()
