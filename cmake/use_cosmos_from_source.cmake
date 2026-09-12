if(NOT DEFINED COSMOS_AGENT_INCLUDED)
set(COSMOS_AGENT_INCLUDED TRUE)

get_filename_component(COSMOS_SOURCE_AGENT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(DEFINED COSMOS_SOURCE)
    # Auto-initialize the lower-layer submodule if not present
    if(NOT EXISTS "${COSMOS_SOURCE}/simulator/CMakeLists.txt")
        message(STATUS "cosmosv5: initializing simulator submodule...")
        execute_process(
            COMMAND git submodule update --init simulator
            WORKING_DIRECTORY "${COSMOS_SOURCE}"
            RESULT_VARIABLE _cosmos_git_result
        )
        if(NOT _cosmos_git_result EQUAL 0)
            message(FATAL_ERROR
                "cosmosv5: simulator submodule is not initialized and auto-init failed.\n"
                "Run from the cosmosv5 workspace root:\n"
                "  git submodule update --init simulator")
        endif()
    endif()
    include(${COSMOS_SOURCE}/simulator/cmake/use_cosmos_from_source.cmake)
else()
    message(FATAL_ERROR "COSMOS_SOURCE not set. Set COSMOS_SOURCE to the cosmosv5 workspace root (the directory containing thirdparty/, kernel/, etc.).")
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
