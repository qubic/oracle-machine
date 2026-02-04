project(price_oracle_service)

# Service sources
set(SERVICE_SOURCES
    ${SERVICE_CORE_DIR}/base_oracle_service.cpp
    src/price_service.cpp
    src/exchange_provider.cpp
    src/binance_provider.cpp
    src/mexc_provider.cpp
    src/gate_provider.cpp
    src/combined_provider.cpp
)

# Create price oracle service executable
add_executable(${PROJECT_NAME}
    src/main.cpp
    ${SERVICE_SOURCES}
)

target_include_directories(${PROJECT_NAME} 
    PRIVATE
        ${SERVICE_CORE_DIR}
        ${CURL_INCLUDE_DIRS}
        ${QUBIC_CORE_DIR}
        ${QUBIC_CORE_DIR}/src
        )

set(SERVICE_LINKED_LIBS 
    ${CURL_LIBRARIES}
    om_common
    om_network)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(${PROJECT_NAME} PRIVATE -mrdrnd -mavx2 -fpermissive)
    set(SERVICE_LINKED_LIBS ${SERVICE_LINKED_LIBS} pthread)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(${PROJECT_NAME} PRIVATE /arch:AVX2)
    set(SERVICE_LINKED_LIBS ${SERVICE_LINKED_LIBS} ws2_32)
else()
    message(FATAL "Unsupported compiler.")
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE ${SERVICE_LINKED_LIBS} )


# Install target
install(TARGETS ${PROJECT_NAME} 
        RUNTIME DESTINATION bin)
