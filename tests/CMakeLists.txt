include(FetchContent)

option(ARGPARSER_USE_SYSTEM_CATCH2 "Use an installed Catch2 instead of FetchContent" OFF)
set(ARGPARSER_CATCH2_SOURCE_DIR "" CACHE PATH
    "Path to a local Catch2 checkout (takes precedence over downloading Catch2)")

if(ARGPARSER_CATCH2_SOURCE_DIR)
    add_subdirectory("${ARGPARSER_CATCH2_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/Catch2")
    list(APPEND CMAKE_MODULE_PATH "${ARGPARSER_CATCH2_SOURCE_DIR}/extras")
elseif(ARGPARSER_USE_SYSTEM_CATCH2)
    find_package(Catch2 3 REQUIRED)
else()
    FetchContent_Declare(Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG v3.8.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
endif()

include(Catch)

add_executable(argparse_tests
    Unittest.test.cpp
    Golden.test.cpp)

target_link_libraries(argparse_tests
    PRIVATE
        Argparser::Argparser
        Catch2::Catch2WithMain)

target_compile_features(argparse_tests PRIVATE cxx_std_17)
target_compile_definitions(argparse_tests PRIVATE
    ARGPARSE_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden")

if(MSVC)
    target_compile_options(argparse_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(argparse_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

catch_discover_tests(argparse_tests
    PROPERTIES
        LABELS unit
        TIMEOUT 60)

# Link the header from three translation units to catch ODR and multiple
# definition regressions. This target intentionally has no Catch2 dependency.
add_executable(argparse_multi_tu
    multi_tu/main.cpp
    multi_tu/a.cpp
    multi_tu/b.cpp)

target_link_libraries(argparse_multi_tu PRIVATE Argparser::Argparser)
target_compile_features(argparse_multi_tu PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(argparse_multi_tu PRIVATE /W4 /permissive-)
else()
    target_compile_options(argparse_multi_tu PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME argparse_multi_tu COMMAND argparse_multi_tu)
set_tests_properties(argparse_multi_tu PROPERTIES
    LABELS unit
    TIMEOUT 30)

if(ARGPARSER_BUILD_BENCHMARKS)
    add_executable(argparse_benchmarks
        Benchmarks.test.cpp)

    target_link_libraries(argparse_benchmarks
        PRIVATE
            Argparser::Argparser
            Catch2::Catch2WithMain)

    target_compile_features(argparse_benchmarks PRIVATE cxx_std_17)

    if(MSVC)
        target_compile_options(argparse_benchmarks PRIVATE /W4 /permissive-)
    else()
        target_compile_options(argparse_benchmarks PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(ARGPARSER_RUN_BENCHMARKS)
        add_test(NAME argparse_benchmarks
            COMMAND argparse_benchmarks "[!benchmark]" --benchmark-samples 20)
        set_tests_properties(argparse_benchmarks PROPERTIES
            LABELS benchmark
            TIMEOUT 300)
    endif()
endif()
