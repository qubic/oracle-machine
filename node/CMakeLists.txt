project(oracle_machine_node)

find_package(Threads REQUIRED)

set(OM_SOURCES
    src/main.cpp
    src/oracle_machine_node.cpp
    src/oracle_cache.cpp
    src/interface_client.cpp
    src/node_connection.cpp
    src/request_handler.cpp
)

add_executable(${PROJECT_NAME} ${OM_SOURCES})

set(OM_NODE_LINKED_LIBS 
    Threads::Threads
    oracle_logger
    oracle_network)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE -mrdrnd -mavx2 -fpermissive)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(OM_NODE_LINKED_LIBS ${OM_NODE_LINKED_LIBS} ws2_32)
    target_compile_options(${PROJECT_NAME} PRIVATE /arch:AVX2)
else()
    message(FATAL "Unsupported compiler.")
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${OM_NODE_LINKED_LIBS}
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/node/src
        ${CMAKE_SOURCE_DIR}/submodules/qubic_core/src
        ${CMAKE_SOURCE_DIR}/submodules/qubic_core
)

# Install target
install(TARGETS ${PROJECT_NAME} 
        RUNTIME DESTINATION bin)