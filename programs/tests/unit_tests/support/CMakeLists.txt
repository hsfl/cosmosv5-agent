cmake_minimum_required(VERSION 3.20)

message(STATUS "${CMAKE_CURRENT_SOURCE_DIR} : ")

add_subdirectory(beacon)
add_subdirectory(convert)
add_subdirectory(namespace)
add_subdirectory(packethandler)
add_subdirectory(transfer)
