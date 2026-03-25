# 获取客户端的源文件
file(GLOB CLIENT_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

add_executable(benchmark_client ${CLIENT_SRCS})
target_link_libraries(benchmark_client PRIVATE core benchmark_proto ${LIBS})
target_compile_features(benchmark_client PRIVATE cxx_std_23)
target_compile_options(benchmark_client PRIVATE -Wall -Wextra -Wpedantic)
set_target_properties(benchmark_client PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR}/bin)
