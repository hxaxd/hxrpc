cmake_minimum_required(VERSION 3.20)
project(hxrpc)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(${CMAKE_SOURCE_DIR}/src/include)
include_directories(${CMAKE_SOURCE_DIR}/example)

find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})

set(PROTO_INCLUDES ${CMAKE_SOURCE_DIR}/src/include)

set(LIBS
    protobuf
    pthread
    zookeeper_mt
    muduo_net
    muduo_base
)

add_subdirectory(src)
add_subdirectory(example)
