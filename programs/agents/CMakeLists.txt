cmake_minimum_required(VERSION 3.20)

file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

message(STATUS "${CMAKE_CURRENT_SOURCE_DIR} : ")
message(${files})

foreach(file ${files})
    get_filename_component(bfile ${file} NAME_WE)
    add_executable(${bfile} ${bfile}.cpp)
    install(TARGETS ${bfile} DESTINATION bin)
endforeach()

target_link_libraries(agent CosmosAgent)
target_link_libraries(agent_cpu CosmosAgent CosmosDeviceDisk)
target_link_libraries(agent_data CosmosAgent)
target_link_libraries(agent_exec CosmosEvent CosmosAgent CosmosCommand)
# agent_file moved to modules repo (uses file_module.h)
target_link_libraries(agent_forward CosmosAgent)
target_link_libraries(agent_monitor CosmosAgent CosmosCommand)
target_link_libraries(agent_route CosmosAgent)
target_link_libraries(agent_time CosmosAgent)
target_link_libraries(agent_tunnel CosmosAgent CosmosDeviceSerial)
target_link_libraries(agent_tunnel2 CosmosAgent CosmosDeviceSerial)

# ground-station agents moved to ground-station repo
add_subdirectory(other)
