find_package(Protobuf REQUIRED)

set(PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/user.proto)
set(PROTO_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/user.pb.cc)

add_library(benchmark_proto STATIC ${PROTO_SRCS})
protobuf_generate(
    TARGET benchmark_proto
    LANGUAGE cpp
    PROTOS ${PROTO_FILES}
    PROTOC_OUT_DIR ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(benchmark_proto PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${Protobuf_INCLUDE_DIRS}
)
target_link_libraries(benchmark_proto PUBLIC protobuf)
target_compile_features(benchmark_proto PUBLIC cxx_std_23)

add_subdirectory(server)
add_subdirectory(client)
