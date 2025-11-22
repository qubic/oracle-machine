project(oracle_machine)

find_package(Threads REQUIRED)

set(OM_SOURCES
    main.cpp
    oracle_machine.cpp
    oracle_client.cpp
    node_connection.cpp
    request_handler.cpp
    oracle_data.cpp
    logger.cpp
)

add_executable(oracle_machine ${OM_SOURCES})

target_compile_definitions(oracle_machine
    PUBLIC
        NETWORK_MESSAGES_WITHOUT_CORE_DEPENDENCIES=1
)

target_link_libraries(oracle_machine
    PRIVATE
        Threads::Threads
)

target_include_directories(oracle_machine
    PRIVATE
        ${CMAKE_SOURCE_DIR}/submodules/qubic_core/src
        ${CMAKE_SOURCE_DIR}/submodules/qubic_core
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(oracle_machine PRIVATE -mrdrnd)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    # Windows needs the Winsock library
    target_link_libraries(oracle_machine PRIVATE ws2_32)
else()
    message(FATAL "Unsupported compiler.")
endif()