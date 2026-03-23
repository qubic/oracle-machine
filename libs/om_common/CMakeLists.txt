project(om_common)
find_package(Threads REQUIRED)

set(OM_COMMON_SOURCES
    src/config.cpp
    src/logger.cpp
    include/om_common/default_config.h
    include/om_common/config.h
    include/om_common/logger.h
    include/om_common/qpi_adapter.h
)

add_library(${PROJECT_NAME} STATIC ${OM_COMMON_SOURCES})

set(OM_COMMON_LINKED_LIBS Threads::Threads)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR CMAKE_CXX_COMPILER_ID MATCHES "GNU")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
else()
    message(FATAL "Unsupported compiler.")
endif()

target_link_libraries(${PROJECT_NAME}
    PRIVATE
        ${OM_COMMON_LINKED_LIBS}
)
target_include_directories(${PROJECT_NAME}
    PRIVATE
        src
)

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)