cmake_minimum_required(VERSION 3.20)

FILE(GLOB INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

# GigE camera library (deps: socketlib, bytelib, vector — all kernel/micro-agent)
add_library(CosmosDeviceGige STATIC gige_lib.cpp gige_lib.h)
target_link_libraries(CosmosDeviceGige CosmosNetwork CosmosMath CosmosSupport ${COSMOS_LIBS})
install(TARGETS CosmosDeviceGige DESTINATION lib)

# A35 thermal camera (depends on gige_lib; agentclass.h in .cpp only)
add_library(CosmosDeviceAcq STATIC acq_a35.cpp acq_a35.h)
target_link_libraries(CosmosDeviceAcq CosmosDeviceGige CosmosAgent CosmosSupport ${COSMOS_LIBS})
install(TARGETS CosmosDeviceAcq DESTINATION lib)

install(FILES ${INCLUDES} DESTINATION include/device/general)

message("Build agent device/general from ${CMAKE_CURRENT_SOURCE_DIR}")
