# protobuf 自动生成
find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})

# 获取当前目录下所有的 proto 文件
file(GLOB PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.proto")

set(PROTO_SRCS)
set(PROTO_HDRS)

# 遍历每个 proto 文件，配置自动生成规则
foreach(PROTO_FILE IN LISTS PROTO_FILES)
    # 获取文件名（不带扩展名）
    get_filename_component(FIL_WE ${PROTO_FILE} NAME_WE)
    
    set(GENERATED_SRC "${CMAKE_CURRENT_SOURCE_DIR}/${FIL_WE}.pb.cc")
    set(GENERATED_HDR "${CMAKE_CURRENT_SOURCE_DIR}/${FIL_WE}.pb.h")
    
    list(APPEND PROTO_SRCS ${GENERATED_SRC})
    list(APPEND PROTO_HDRS ${GENERATED_HDR})

    # 当 pb.cc 或 pb.h 缺失时，自动调用 protoc 生成
    add_custom_command(
        OUTPUT ${GENERATED_SRC} ${GENERATED_HDR}
        COMMAND ${Protobuf_PROTOC_EXECUTABLE}
                --cpp_out=${CMAKE_CURRENT_SOURCE_DIR}
                -I${CMAKE_CURRENT_SOURCE_DIR}
                ${PROTO_FILE}
        DEPENDS ${PROTO_FILE}
        COMMENT "Running C++ protocol buffer compiler on ${PROTO_FILE}"
        VERBATIM
    )
endforeach()

# 核心修复：将生成的 pb.cc 打包成一个独立的静态库 example_proto
add_library(example_proto STATIC ${PROTO_SRCS})
# 开启 -fPIC，防止链接到可执行文件时出现 DT_TEXTREL 警告
set_target_properties(example_proto PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 包含子目录
add_subdirectory(callee)
add_subdirectory(caller)