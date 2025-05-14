cmake_minimum_required(VERSION 3.27.2)

#-------------------------------------------------------------------------

set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
    CACHE STRING "Vcpkg toolchain file"
)

project(taosim CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

option(BUILD_TESTS "Build tests" ON)
option(BUILD_BENCHMARKS "Build benchmarks" OFF)

option(ASAN "Enable address sanitizer (requires UNIX)" OFF)
if(ASAN AND NOT UNIX)
    message(FATAL_ERROR "ASan unavailable on ${CMAKE_SYSTEM_NAME}")
endif()

option(PARALLEL_QUEUES "Enable multithreading over books" ON)
option(PROF "Enable profiling codegen" OFF)

set(src_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/cpp" CACHE STRING "C++ source directory")
set(internal_compile_options "")
set(internal_compile_definitions "")
set(internal_link_options "")

if(ASAN)
    LIST(APPEND internal_compile_options "-fsanitize=address")
    LIST(APPEND internal_link_options "-fsanitize=address")
endif()

if(PARALLEL_QUEUES)
    set(parallel_flag 1)
else()
    set(parallel_flag 0)
endif()
LIST(APPEND internal_compile_definitions PARALLEL_QUEUES=${parallel_flag})

if(PROF)
    set(prof_flag 1)
else()
    set(prof_flag 0)
endif()
LIST(APPEND internal_compile_definitions PROF=${prof_flag})

set(internal_compile_options_cache "${internal_compile_options}" CACHE STRING "" FORCE)
set(internal_copmile_definitions_cache "${internal_compile_definitions}" CACHE STRING "" FORCE)
set(internal_link_options_cache "${internal_link_options}" CACHE STRING "" FORCE)

#-------------------------------------------------------------------------

find_package(Boost REQUIRED)
find_package(CLI11 CONFIG REQUIRED)
find_package(Eigen3 CONFIG REQUIRED)
find_package(bdl CONFIG REQUIRED)
find_package(date CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)
find_package(pybind11 CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

#-------------------------------------------------------------------------

add_subdirectory("${src_dir}")

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(test/cpp-tests)
endif()

if(BUILD_BENCHMARKS)
    add_subdirectory(benchmark)
endif()

add_custom_target(check_compiler
    COMMAND ${CMAKE_COMMAND} -E echo
        "C++ Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
)

#-------------------------------------------------------------------------
