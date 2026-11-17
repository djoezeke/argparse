add_executable(argparse_demo Demo.cpp)
target_link_libraries(argparse_demo PRIVATE Argparser::Argparser)
target_compile_features(argparse_demo PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(argparse_demo PRIVATE /W4 /permissive-)
else()
    target_compile_options(argparse_demo PRIVATE -Wall -Wextra -Wpedantic)
endif()
