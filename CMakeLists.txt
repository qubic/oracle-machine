cmake_minimum_required(VERSION 3.21)
project(oracle_machine)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

if(CMAKE_CMAKE_BUILD_TYPE MATCHES DEBUG)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
endif()

# Additional cmake flags
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
else()
    message(FATAL "Unsupported compiler.")
endif()

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Thirdparty module
find_package(yaml-cpp)
if (yaml-cpp_FOUND)
    message(STATUS "Found yaml-cpp")
    message(STATUS "- yaml-cpp_LIBRARIES: ${yaml-cpp_LIBRARIES}")
    message(STATUS "- yaml-cpp_INCLUDE_DIRS: ${yaml-cpp_INCLUDE_DIRS}")
else()
    message(STATUS "yaml-cpp: NOT FOUND")
endif()

add_subdirectory(logger)
add_subdirectory(network)
add_subdirectory(node)
add_subdirectory(oracles)