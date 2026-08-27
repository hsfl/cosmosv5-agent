if(NOT DEFINED COSMOS_AGENT_INCLUDED)
set(COSMOS_AGENT_INCLUDED TRUE)

get_filename_component(COSMOS_SOURCE_AGENT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(EXISTS "${COSMOS_SOURCE_AGENT}/deps/simulator/cmake/use_cosmos_from_source.cmake")
    include(${COSMOS_SOURCE_AGENT}/deps/simulator/cmake/use_cosmos_from_source.cmake)
elseif(DEFINED COSMOS_SOURCE)
    include(${COSMOS_SOURCE}/simulator/cmake/use_cosmos_from_source.cmake)
else()
    message(FATAL_ERROR "Cannot find cosmosv5-simulator. Clone with --recurse-submodules or set COSMOS_SOURCE.")
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
