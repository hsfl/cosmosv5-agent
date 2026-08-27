if(NOT DEFINED COSMOS_AGENT_INCLUDED)
set(COSMOS_AGENT_INCLUDED TRUE)

get_filename_component(COSMOS_SOURCE_AGENT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(DEFINED COSMOS_SOURCE)
    include(${COSMOS_SOURCE}/simulator/cmake/use_cosmos_from_source.cmake)
else()
    message(FATAL_ERROR "COSMOS_SOURCE not set. Clone the cosmosv5 workspace with --recurse-submodules or set COSMOS_SOURCE to the workspace root.")
endif()

message("Using COSMOS agent from " ${COSMOS_SOURCE_AGENT})

include_directories(${COSMOS_SOURCE_AGENT}/libraries)
include_directories(${COSMOS_SOURCE_AGENT}/libraries/device)

add_subdirectory(${COSMOS_SOURCE_AGENT}/libraries/physics            ${CMAKE_BINARY_DIR}/agent/libraries/physics)
add_subdirectory(${COSMOS_SOURCE_AGENT}/libraries/support            ${CMAKE_BINARY_DIR}/agent/libraries/support)
add_subdirectory(${COSMOS_SOURCE_AGENT}/libraries/agent              ${CMAKE_BINARY_DIR}/agent/libraries/agent)
# modules live in cosmosv5-modules — not built here
add_subdirectory(${COSMOS_SOURCE_AGENT}/libraries/device/general     ${CMAKE_BINARY_DIR}/agent/libraries/device/general)

endif()
