if(NOT DEFINED COSMOS_AGENT_INCLUDED)
set(COSMOS_AGENT_INCLUDED TRUE)

include(${COSMOS_SOURCE}/simulator/cmake/use_cosmos_from_source.cmake)

set(COSMOS_SOURCE_AGENT ${COSMOS_SOURCE}/agent)
message("Using COSMOS agent from " ${COSMOS_SOURCE_AGENT})

include_directories(${COSMOS_SOURCE_AGENT}/libraries)
include_directories(${COSMOS_SOURCE_AGENT}/libraries/device)

add_subdirectory(${COSMOS_SOURCE_AGENT}/libraries/physics            ${CMAKE_BINARY_DIR}/agent/libraries/physics)
add_subdirectory(${COSMOS_SOURCE_AGENT}/libraries/support            ${CMAKE_BINARY_DIR}/agent/libraries/support)
add_subdirectory(${COSMOS_SOURCE_AGENT}/libraries/agent              ${CMAKE_BINARY_DIR}/agent/libraries/agent)
# modules live in cosmos/src/modules/ — not built here
add_subdirectory(${COSMOS_SOURCE_AGENT}/libraries/device/general     ${CMAKE_BINARY_DIR}/agent/libraries/device/general)

endif()
