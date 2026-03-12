# 获取服务端的源文件
file(GLOB SERVER_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

# 创建服务端可执行文件
add_executable(server ${SERVER_SRCS})

# 链接必要的库：core, example_proto(刚才生成的), 以及全局依赖 LIBS
target_link_libraries(server core example_proto ${LIBS})

# 设置编译选项（使用根目录的 C++23 标准，只需保留 -Wall 警告即可）
target_compile_options(server PRIVATE -Wall)

# 设置 server 可执行文件输出目录
set_target_properties(server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)