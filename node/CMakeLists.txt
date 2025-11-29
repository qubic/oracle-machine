project(oracle_machine_node)

find_package(Threads REQUIRED)

set(OM_NETWORK_SOURCES
    src/network/tcp_client.cpp
    src/network/tcp_server.cpp
    src/network/session.cpp
)
set(OM_SOURCES
    src/main.cpp
    src/oracle_machine_node.cpp
    src/oracle_cache.cpp
    src/interface_client.cpp
    src/node_connection.cpp
    src/request_handler.cpp
    src/logger.cpp
    ${OM_NETWORK_SOURCES}
)

add_executable(${PROJECT_NAME} ${OM_SOURCES})

# target_compile_definitions(${PROJECT_NAME}
#     PUBLIC
#         #NETWORK_MESSAGES_WITHOUT_CORE_DEPENDENCIES=1
#         NO_UEFI=1
# )

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        Threads::Threads
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/node/src
        ${CMAKE_SOURCE_DIR}/submodules/qubic_core/src
        ${CMAKE_SOURCE_DIR}/submodules/qubic_core
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE -mrdrnd -mavx2 -fpermissive)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(${PROJECT_NAME} PRIVATE /arch:AVX2)
    # Windows needs the Winsock library
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
else()
    message(FATAL "Unsupported compiler.")
endif()