# 获取服务端的源文件
file(GLOB SERVER_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

add_executable(benchmark_server ${SERVER_SRCS})
target_link_libraries(benchmark_server PRIVATE core benchmark_proto ${LIBS})
target_compile_features(benchmark_server PRIVATE cxx_std_23)
target_compile_options(benchmark_server PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(benchmark_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
