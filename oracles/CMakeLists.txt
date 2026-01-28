project(oracle_services)

set(ORACLE_NODE_DIR ${CMAKE_SOURCE_DIR}/node/src)

# Qubic-core
set(QUBIC_CORE_DIR ${CMAKE_SOURCE_DIR}/submodules/qubic_core)

# Service core
set(SERVICE_CORE_DIR ${CMAKE_SOURCE_DIR}/oracles/core)

find_package(CURL)
if (CURL_FOUND)
    message(STATUS "Found libcurl")
    message(STATUS "- CURL_LIBRARIES: ${CURL_LIBRARIES}")
    message(STATUS "- CURL_INCLUDE_DIRS: ${CURL_INCLUDE_DIRS}")
else()
    message(STATUS "libcurl: NOT FOUND")
endif()

# Mock oracle
message(STATUS "Build mock service")
add_subdirectory(mock)

# Below services depend on CURL
if(CURL_FOUND)
    message(STATUS "Build price service")
    add_subdirectory(price)
endif()

