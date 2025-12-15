cmake_minimum_required(VERSION 3.16)

project(HotWordsCount LANGUAGES CXX)

# Configurable options
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Option: choose which main to build
option(BUILD_DEMO "Build demo.cpp target" OFF)

# Include headers in cppjieba and its limonp subdir
include_directories(
    ${CMAKE_SOURCE_DIR}/cppjieba
    ${CMAKE_SOURCE_DIR}/cppjieba/limonp
    ${CMAKE_SOURCE_DIR}/third_party/utfcpp/source
)

# Pass dictionary directory as a compile definition so code can find resources
add_compile_definitions(
    JIEBA_DICT_DIR="${CMAKE_SOURCE_DIR}/dict"
    PROJECT_ROOT_DIR="${CMAKE_SOURCE_DIR}"
    INPUT_ROOT_DIR="${CMAKE_SOURCE_DIR}/input"
    OUTPUT_ROOT_DIR="${CMAKE_SOURCE_DIR}/output"
)


# Main app from scripts/main.cpp
add_executable(hotwords
    ${CMAKE_SOURCE_DIR}/scripts/main.cpp
)


# Optional demo target
if(BUILD_DEMO)
    add_executable(demo
        ${CMAKE_SOURCE_DIR}/demo.cpp
    )
    target_compile_definitions(demo PRIVATE JIEBA_DICT_DIR="${CMAKE_SOURCE_DIR}/dict")
endif()

# Install step (optional): place binaries in bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(CMAKE_BUILD_TYPE Debug)

# Helpful post-build message
add_custom_target(info ALL
    COMMAND ${CMAKE_COMMAND} -E echo "Built targets in: ${CMAKE_BINARY_DIR}"
)