# 收集服务端示例源码 (当前目录下全部 .cc) 
file(GLOB SERVER_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

# 生成 benchmark 服务端程序, 并链接核心库与示例 proto 库
add_executable(benchmark_server ${SERVER_SRCS})
target_link_libraries(benchmark_server PRIVATE core benchmark_proto ${LIBS})

# 统一编译标准与告警等级, 便于示例代码质量检查
target_compile_features(benchmark_server PRIVATE cxx_std_23)
target_compile_options(benchmark_server PRIVATE -Wall -Wextra -Wpedantic)

# 将可执行文件输出到仓库 bin 目录, 便于与客户端配套演示
set_target_properties(benchmark_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
