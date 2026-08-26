cmake_minimum_required(VERSION 3.20)

FILE(GLOB INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

# task: lightweight async task runner (deps: datalib, timelib)
add_library(CosmosTask STATIC task.cpp task.h)
target_link_libraries(CosmosTask CosmosData CosmosTime CosmosSupport ${COSMOS_LIBS})
install(TARGETS CosmosTask DESTINATION lib)

# event: scheduled event with JSON serialization
add_library(CosmosEvent STATIC event.cpp event.h)
target_link_libraries(CosmosEvent CosmosNamespace CosmosTime CosmosSupport ${COSMOS_LIBS})
install(TARGETS CosmosEvent DESTINATION lib)

# agentclass: core agent framework
add_library(CosmosAgent STATIC agentclass.cpp agentclass.h)
target_link_libraries(CosmosAgent CosmosLog CosmosTask CosmosChannel CosmosPacket CosmosBeacon CosmosDeviceCpu CosmosJson CosmosNamespace CosmosSupport ${COSMOS_LIBS})
install(TARGETS CosmosAgent DESTINATION lib)

# command_queue: agent command management
add_library(CosmosCommand STATIC command_queue.cpp command_queue.h)
target_link_libraries(CosmosCommand CosmosAgent CosmosEvent CosmosTask CosmosSupport ${COSMOS_LIBS})
install(TARGETS CosmosCommand DESTINATION lib)

# scheduler: time-based event scheduling
add_library(CosmosScheduler STATIC scheduler.cpp scheduler.h)
target_link_libraries(CosmosScheduler CosmosAgent CosmosEvent CosmosSupport ${COSMOS_LIBS})
install(TARGETS CosmosScheduler DESTINATION lib)

install(FILES ${INCLUDES} DESTINATION include/agent)
