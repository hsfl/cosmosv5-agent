cmake_minimum_required(VERSION 3.20)

file(GLOB files "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")

message(STATUS "${CMAKE_CURRENT_SOURCE_DIR} : ")
message(${files})

foreach(file ${files})
    get_filename_component(bfile ${file} NAME_WE)
    add_executable(${bfile} ${bfile}.cpp)
    install(TARGETS ${bfile} DESTINATION bin)
endforeach()

target_link_libraries(agent_arduino CosmosAgent CosmosCssl CosmosDeviceArduino CosmosSimulator CosmosPhysics)
target_link_libraries(agent_node CosmosAgent CosmosPhysics CosmosDeviceAcq)
target_link_libraries(agent_physics CosmosAgent CosmosPhysics CosmosDeviceAcq)
