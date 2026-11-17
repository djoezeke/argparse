cmake_minimum_required(VERSION 3.20)

project(Argparser
    VERSION 0.1.0
    DESCRIPTION "Modern single-file C++ command line argument parser"
    HOMEPAGE_URL "https://github.com/djoezeke/argparse"
    LANGUAGES CXX)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

option(ARGPARSER_BUILD_TESTS "Build the Catch2 unit tests" OFF)
option(ARGPARSER_BUILD_BENCHMARKS "Build the Catch2 microbenchmarks (requires tests)" ON)
option(ARGPARSER_RUN_BENCHMARKS "Register the microbenchmarks with CTest (slow)" OFF)
option(ARGPARSER_BUILD_EXAMPLES "Build the example programs" OFF)
option(ARGPARSER_ENABLE_INSTALL "Generate install and CMake package rules" OFF)

add_library(argparse INTERFACE)
add_library(Argparser::Argparser ALIAS argparse)
add_library(argparse::argparse ALIAS argparse)

# The installed package exposes the target as Argparser::Argparser, matching
# the alias used by consumers that add this repository as a subdirectory.
set_target_properties(argparse PROPERTIES EXPORT_NAME Argparser)

target_compile_features(argparse INTERFACE cxx_std_17)
target_include_directories(argparse INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

# The library is a single header located at the repository root. Keep the
# source-tree include path stable for examples, tests, and downstream users.
set(ARGPARSER_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/argparse.h")

if(ARGPARSER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(ARGPARSER_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(ARGPARSER_ENABLE_INSTALL)
    install(TARGETS argparse EXPORT ArgparserTargets)

    install(FILES "${ARGPARSER_HEADER}"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

    install(EXPORT ArgparserTargets
        FILE ArgparserTargets.cmake
        NAMESPACE Argparser::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Argparser")

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ArgparserConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/ArgparserConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Argparser")

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/ArgparserConfigVersion.cmake"
        VERSION "${PROJECT_VERSION}"
        COMPATIBILITY SameMajorVersion)

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/ArgparserConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/ArgparserConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/Argparser")
endif()
