#设置最低版本和项目名称
cmake_minimum_required(VERSION 3.0)
project(hxrpc)

#设置全局的C++标准
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)

include_directories(${CMAKE_SOURCE_DIR}/src/include)

find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})

set(LIBS
    protobuf
    pthread
    zookeeper_mt
    muduo_net
    muduo_base
    glog
)

add_subdirectory(src)
add_subdirectory(example)
