cmake_minimum_required(VERSION 3.20)

add_library(yoyo_sample_cpp SHARED plugin.cpp)
target_include_directories(yoyo_sample_cpp PRIVATE "${PROJECT_SOURCE_DIR}/include")
set_target_properties(yoyo_sample_cpp PROPERTIES
    PREFIX ""
    OUTPUT_NAME "sample_cpp"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/plugins"
)

if (WIN32)
    target_compile_options(yoyo_sample_cpp PRIVATE /utf-8 /W4 /permissive- /EHsc)
else()
    target_compile_options(yoyo_sample_cpp PRIVATE -Wall -Wextra -fPIC)
    if (YOYO_STATIC_CXX_RUNTIME)
        target_link_options(yoyo_sample_cpp PRIVATE -static-libstdc++ -static-libgcc)
    endif()
endif()
