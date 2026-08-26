cmake_minimum_required(VERSION 3.20)

FILE(GLOB INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/*.h)

# physicslib: orbital mechanics functions (needs convertlib, demlib, nrlmsise, jsondef)
add_library(CosmosPhysicsLib STATIC physicslib.cpp physicslib.h physicsdef.h)
target_link_libraries(CosmosPhysicsLib CosmosConvert CosmosDem CosmosNrlmsise CosmosNamespace CosmosMath CosmosSupport ${COSMOS_LIBS})
install(TARGETS CosmosPhysicsLib DESTINATION lib)

# physicsclass: spacecraft physics simulation (needs jsonlib, convertlib, demlib, nrlmsise)
add_library(CosmosPhysics STATIC physicsclass.cpp physicsclass.h)
target_link_libraries(CosmosPhysics CosmosPhysicsLib CosmosConvert CosmosEnum CosmosDem CosmosNrlmsise CosmosNamespace CosmosData CosmosMath CosmosSupport ${COSMOS_LIBS})
install(TARGETS CosmosPhysics DESTINATION lib)

# simulatorclass: full simulation environment (needs physicsclass, jsondef, convertlib)
add_library(CosmosSimulator STATIC simulatorclass.cpp simulatorclass.h)
target_link_libraries(CosmosSimulator CosmosPhysics CosmosConvert CosmosNamespace CosmosSupport ${COSMOS_LIBS})
install(TARGETS CosmosSimulator DESTINATION lib)

install(FILES ${INCLUDES} DESTINATION include/physics)
