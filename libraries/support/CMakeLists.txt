cmake_minimum_required(VERSION 3.20)

FILE(GLOB INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

# convertlib: coordinate conversions (needs geomag, ephemlib, jpleph, demlib + jsondef for json_out_*)
add_library(CosmosConvert STATIC convertlib.cpp convertlib.h)
target_link_libraries(CosmosConvert CosmosGeomag CosmosEphem CosmosJpleph CosmosDem CosmosData CosmosTime CosmosMath CosmosSupport ${COSMOS_LIBS})
install(TARGETS CosmosConvert DESTINATION lib)

# enumlib: device type enum strings (needs convertlib)
add_library(CosmosEnum STATIC enumlib.cpp enumlib.h)
target_link_libraries(CosmosEnum CosmosConvert CosmosSupport ${COSMOS_LIBS})
install(TARGETS CosmosEnum DESTINATION lib)

# objlib: 3D object/CAD support (deps: datalib, math — all micro-agent/kernel)
add_library(CosmosCad STATIC objlib.cpp objlib.h)
target_link_libraries(CosmosCad CosmosData CosmosMath CosmosSupport ${COSMOS_LIBS})
install(TARGETS CosmosCad DESTINATION lib)

# COSMOS namespace: jsondef + physicsdef + jsonlib
# Note: jsonlib.cpp calls Convert:: functions (convertlib, simulator layer).
# CosmosConvert is NOT listed here — it is added by simulator's CMakeLists.
# Final executables must link CosmosConvert from simulator.
add_library(CosmosNamespace STATIC
    jsondef.h jsondef.cpp
    jsonlib.h jsonlib.cpp)
target_link_libraries(CosmosNamespace CosmosConvert CosmosCad CosmosJson CosmosString CosmosMath CosmosSupport ${COSMOS_LIBS})
install(TARGETS CosmosNamespace DESTINATION lib)

# Beacon: packet-level telemetry beacon (enumlib include removed — was dead)
add_library(CosmosBeacon STATIC beacon.cpp beacon.h)
target_link_libraries(CosmosBeacon CosmosPacket CosmosNamespace CosmosMath CosmosSupport ${COSMOS_LIBS})
install(TARGETS CosmosBeacon DESTINATION lib)

# Packet handler: routes packets to agents; needs convertlib (simulator) + agentclass
add_library(CosmosPacketHandler STATIC packethandler.cpp packethandler.h)
target_link_libraries(CosmosPacketHandler CosmosBeacon CosmosPacket CosmosConvert CosmosAgent CosmosNamespace CosmosSupport ${COSMOS_LIBS})
install(TARGETS CosmosPacketHandler DESTINATION lib)

# Transfer class (depends on agentclass — built after CosmosAgent in agent/)
add_library(CosmosTransfer STATIC transferclass.h transferclass.cpp)
target_link_libraries(CosmosTransfer CosmosAgent CosmosPacket CosmosData CosmosSupport ${COSMOS_LIBS})
install(TARGETS CosmosTransfer DESTINATION lib)

# File/UDP sender utilities (depend on CosmosTransfer + CosmosNetwork)
add_library(FileSenderImpl STATIC FileSender.cpp FileSender.h UdpSender.cpp UdpSender.h)
target_link_libraries(FileSenderImpl CosmosTransfer CosmosNetwork ${COSMOS_LIBS})
install(TARGETS FileSenderImpl DESTINATION lib)

install(FILES ${INCLUDES} DESTINATION include/support)
