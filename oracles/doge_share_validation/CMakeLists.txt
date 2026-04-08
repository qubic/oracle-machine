project(doge_oracle_service)

# Service sources
set(DOGE_SERVICE_SOURCES
    ${SERVICE_CORE_DIR}/base_oracle_service.cpp
    src/doge_service.cpp
    src/doge_service.h
    src/scrypt.c
    src/scrypt.h
)

# Create doge oracle service executable
add_executable(${PROJECT_NAME}
    src/main.cpp
    ${DOGE_SERVICE_SOURCES}
)

target_include_directories(${PROJECT_NAME} 
    PRIVATE
        ${SERVICE_CORE_DIR}
        ${CURL_INCLUDE_DIRS}
        ${QUBIC_CORE_DIR}
        ${QUBIC_CORE_DIR}/src
        )

set(SERVICE_LINKED_LIBS
    om_common
    om_network)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    # scrypt.c uses aggressive pointer casting that breaks under strict aliasing at -O2.
    set_source_files_properties(src/scrypt.c PROPERTIES COMPILE_OPTIONS "-fno-strict-aliasing")
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
