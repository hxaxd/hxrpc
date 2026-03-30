# 收集 src 目录下全部实现文件, 统一编入 core 静态库
file(GLOB SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

# 核心库依赖 protobuf (框架头部与编解码使用) 
find_package(Protobuf REQUIRED)

# 生成核心静态库, 供 server/client/tests 复用
add_library(core STATIC ${SRC_FILES})

# 定义框架级 proto (header.proto) , 并将生成的 .pb.cc/.pb.h 自动加入 core
set(PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/header.proto)
protobuf_generate(TARGET core LANGUAGE cpp PROTOS ${PROTO_FILES})

# core 的公开头文件目录: 
# - include: 手写对外接口
# - binary dir: protobuf 生成头
# - protobuf include: 第三方依赖头
target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
)

# 对外链接 protobuf, 并声明编译标准与警告级别
target_link_libraries(core PUBLIC protobuf)
target_compile_features(core PUBLIC cxx_std_23)
target_compile_options(core PRIVATE -Wall -Wextra -Wpedantic)
