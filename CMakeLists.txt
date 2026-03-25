cmake_minimum_required(VERSION 3.20)
project(hxrpc)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

include_directories(${CMAKE_SOURCE_DIR}/src/include)
include_directories(${CMAKE_SOURCE_DIR}/benchmark)

find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})

set(PROTO_INCLUDES ${CMAKE_SOURCE_DIR}/src/include)

set(LIBS
    protobuf
    pthread
    zookeeper_mt
)

add_subdirectory(src)
add_subdirectory(benchmark)
enable_testing()
add_subdirectory(tests)
