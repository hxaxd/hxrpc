# benchmark 目录用于构建示例客户端/服务端与其专用 protobuf
find_package(Protobuf REQUIRED)

# 示例业务 proto (user.proto) , 生成 user.pb.cc/.h
set(PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/user.proto)
set(PROTO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/user.pb.cc)

# 将示例 proto 构建为独立静态库, 方便 tests 与 benchmark 目标共享
add_library(benchmark_proto STATIC ${PROTO_SRCS})
protobuf_generate(
    TARGET benchmark_proto
    LANGUAGE cpp
    PROTOS ${PROTO_FILES}
    PROTOC_OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}
)

# 导出包含目录: 
# - 当前目录 (user.pb.h) ；
# - protobuf 官方头文件目录
target_include_directories(benchmark_proto PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Protobuf_INCLUDE_DIRS}
)

# 链接 protobuf 并固定 C++ 标准
target_link_libraries(benchmark_proto PUBLIC protobuf)
target_compile_features(benchmark_proto PUBLIC cxx_std_23)

# 构建示例服务端与客户端可执行程序
add_subdirectory(server)
add_subdirectory(client)
