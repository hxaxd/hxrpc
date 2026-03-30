# 指定 CMake 最低版本, 确保使用项目依赖的命令特性
cmake_minimum_required(VERSION 3.20)

# 定义工程名, 后续 target 的默认命名空间与工程变量都基于此
project(hxrpc)

# 统一 C++ 标准与编译行为: 
# - 使用 C++23
# - 强制要求编译器支持该标准
# - 关闭编译器扩展, 尽量保持跨平台一致性
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)

# 头文件搜索路径: 
# - src/include 放核心库对外头文件
# - benchmark 放示例 proto 生成头 (如 user.pb.h) 
include_directories(${CMAKE_SOURCE_DIR}/src/include)
include_directories(${CMAKE_SOURCE_DIR}/benchmark)

# 查找 protobuf 依赖, 并把其头文件目录加入全局包含路径
find_package(Protobuf REQUIRED)
include_directories(${Protobuf_INCLUDE_DIRS})

# 记录项目 proto 头文件目录 (供子模块复用) 
set(PROTO_INCLUDES ${CMAKE_SOURCE_DIR}/src/include)

# 统一第三方/系统库集合, 供 tests/benchmark 子目录直接复用
# 说明: zookeeper_mt 为 ZooKeeper 多线程客户端库
set(LIBS
    protobuf
    pthread
    zookeeper_mt
)

# 按模块组织构建: 
# - src: 核心静态库
# - benchmark: 示例客户端/服务端与 proto
# - tests: 单元与集成测试
add_subdirectory(src)
add_subdirectory(benchmark)

# 开启 CTest, 并注册 tests 子目录中的测试目标
enable_testing()
add_subdirectory(tests)
