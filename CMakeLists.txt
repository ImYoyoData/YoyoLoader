cmake_minimum_required(VERSION 3.20)
project(YoyoLoader VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(YOYO_QUICKJS_SOURCES
    third_party/quickjs/quickjs.c
    third_party/quickjs/dtoa.c
    third_party/quickjs/libregexp.c
    third_party/quickjs/libunicode.c
)

option(YOYO_STATIC_CXX_RUNTIME "Statically link libstdc++ into the Linux runtime" ON)
option(YOYO_BUILD_SAMPLES "Build the bundled sample plugins" ON)

set(YOYO_RUNTIME_SOURCES
    src/runtime/entry.cpp
    src/runtime/runtime.cpp
    src/runtime/log.cpp
    src/runtime/module.cpp
    src/runtime/native_string.cpp
    src/runtime/version.cpp
    src/runtime/hook_engine.cpp
    src/runtime/hooks.cpp
    src/runtime/events.cpp
    src/runtime/plugin_loader.cpp
    src/runtime/players.cpp
    src/runtime/nbt/nbt.cpp
    src/runtime/js/js_engine.cpp
    src/runtime/js/js_nbt.cpp
    src/runtime/js/js_forms.cpp
    ${YOYO_QUICKJS_SOURCES}
)

if (WIN32)
    add_library(yoyo_runtime SHARED ${YOYO_RUNTIME_SOURCES})
    target_compile_definitions(yoyo_runtime PRIVATE YOYO_PLATFORM_WINDOWS=1 YOYO_RUNTIME_BUILD=1)
    target_compile_options(yoyo_runtime PRIVATE /utf-8 /W4
        $<$<COMPILE_LANGUAGE:CXX>:/permissive->
        $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
        $<$<COMPILE_LANGUAGE:C>:/std:c11>)
    target_include_directories(yoyo_runtime PRIVATE include src/runtime third_party/quickjs)
    set_target_properties(yoyo_runtime PROPERTIES OUTPUT_NAME yoyo_runtime PREFIX "" FOLDER runtime)

    add_executable(yoyo src/launcher/main.cpp src/launcher/win_inject.cpp)
    target_compile_definitions(yoyo PRIVATE YOYO_PLATFORM_WINDOWS=1)
    target_compile_options(yoyo PRIVATE /utf-8 /W4 /permissive- /EHsc)
    target_include_directories(yoyo PRIVATE include src/launcher)
    set_target_properties(yoyo PROPERTIES FOLDER launcher)
else()
    find_package(Threads REQUIRED)

    add_library(yoyo_runtime SHARED ${YOYO_RUNTIME_SOURCES})
    target_compile_definitions(yoyo_runtime PRIVATE YOYO_PLATFORM_LINUX=1 YOYO_RUNTIME_BUILD=1)
    target_compile_options(yoyo_runtime PRIVATE -Wall -Wextra -fPIC -fvisibility=hidden)
    target_include_directories(yoyo_runtime PRIVATE include src/runtime third_party/quickjs)
    target_link_libraries(yoyo_runtime PRIVATE dl Threads::Threads m)
    set_target_properties(yoyo_runtime PROPERTIES PREFIX "lib" OUTPUT_NAME yoyo_runtime FOLDER runtime)
    if (YOYO_STATIC_CXX_RUNTIME)
        target_link_options(yoyo_runtime PRIVATE -static-libstdc++ -static-libgcc)
    endif()

    add_executable(yoyo src/launcher/main.cpp src/launcher/linux_exec.cpp)
    target_compile_definitions(yoyo PRIVATE YOYO_PLATFORM_LINUX=1)
    target_compile_options(yoyo PRIVATE -Wall -Wextra)
    target_include_directories(yoyo PRIVATE include src/launcher)
    set_target_properties(yoyo PROPERTIES FOLDER launcher)
    if (YOYO_STATIC_CXX_RUNTIME)
        target_link_options(yoyo PRIVATE -static-libstdc++ -static-libgcc)
    endif()
endif()

set_target_properties(yoyo_runtime yoyo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

if (YOYO_BUILD_SAMPLES)
    add_subdirectory(plugins/sample_cpp)
endif()
