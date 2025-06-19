cmake_minimum_required(VERSION 3.27.2)

if(NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake"
        CACHE STRING "Vcpkg toolchain file"
    )
endif()

project(taosim CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)

option(BUILD_BENCHMARKS "Build benchmarks" OFF)

set(src_dir "${CMAKE_CURRENT_SOURCE_DIR}/src/cpp" CACHE STRING "C++ source directory")

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
