project(oracle_network)
find_package(Threads REQUIRED)

set(OM_NETWORK_SOURCES
    src/tcp_client.cpp
    src/tcp_server.cpp
    src/session.cpp
)

add_library(${PROJECT_NAME} STATIC ${OM_NETWORK_SOURCES})

set(OM_NETWORK_LINKED_LIBS 
    oracle_logger
    Threads::Threads)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(OM_NETWORK_LINKED_LIBS ${OM_NETWORK_LINKED_LIBS} ws2_32)
else()
    message(FATAL "Unsupported compiler.")
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${OM_NETWORK_LINKED_LIBS}
)

target_include_directories(${PROJECT_NAME}
    PRIVATE
        ${CMAKE_SOURCE_DIR}/network/src
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)