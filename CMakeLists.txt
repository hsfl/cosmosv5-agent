cmake_minimum_required(VERSION 3.20)
project(cosmos-agent)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED YES)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(COSMOS_SOURCE ${CMAKE_SOURCE_DIR}/..)
set(COSMOS_LIBS pthread)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/home2/pilger/cosmos" CACHE PATH "Install prefix" FORCE)
endif()

include(cmake/use_cosmos_from_source.cmake)

add_subdirectory(programs)
