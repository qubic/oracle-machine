project(qubic_test_node)

# Qubic core directory
set(QUBIC_CORE_DIR ${CMAKE_SOURCE_DIR}/submodules/qubic_core/src)

# Create test node executable
add_executable(${PROJECT_NAME}
    src/main.cpp
)

# Include directories
target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/libs/om_common/include
        ${QUBIC_CORE_DIR}
        ${QUBIC_CORE_DIR}/..
)

# Compiler options
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -mrdrnd -mavx2 -fpermissive)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /arch:AVX2)
endif()

# Install target
install(TARGETS ${PROJECT_NAME}
        RUNTIME DESTINATION bin)
