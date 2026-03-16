#获取当前目录下的所有源文件
file(GLOB SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

# protobuf 自动生成
find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 获取 protobuf 的生成文件
file(GLOB PROTO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.pb.cc)

#创建静态库或共享库
add_library(core STATIC ${SRC_FILES} ${PROTO_SRCS})

# 生成 protobuf 文件（必须在 add_library 之后）
set(PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/header.proto)
protobuf_generate(TARGET core LANGUAGE cpp PROTOS ${PROTO_FILES})

#链接protobuf库
target_link_libraries(core PUBLIC protobuf)

#设置头文件的路径（包括 build 目录，因为 protobuf 生成的文件在那里）
target_include_directories(core PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
)

# 添加编译选项
target_compile_options(core PRIVATE -std=c++23 -Wall)