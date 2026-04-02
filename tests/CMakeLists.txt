# -------------------------------
# 配置解析测试: 覆盖 YAML 读取与 typed settings 映射
# -------------------------------
add_executable(config_test config_test.cc)
target_link_libraries(config_test PRIVATE core ${LIBS})
add_test(NAME config_test COMMAND config_test)

# -------------------------------
# 协议编解码测试: 验证请求/响应帧回环正确性
# -------------------------------
add_executable(codec_test codec_test.cc)
target_link_libraries(codec_test PRIVATE core ${LIBS})
add_test(NAME codec_test COMMAND codec_test)

# -------------------------------
# 服务发现测试: 静态发现与发现配置行为
# -------------------------------
add_executable(discovery_test discovery_test.cc)
target_link_libraries(discovery_test PRIVATE core ${LIBS})
add_test(NAME discovery_test COMMAND discovery_test)

# -------------------------------
# 分发器测试: 验证 service/method 路由与调用链
# 依赖 benchmark_proto (测试用 protobuf 服务定义) 
# -------------------------------
add_executable(dispatcher_test dispatcher_test.cc)
target_link_libraries(dispatcher_test PRIVATE core benchmark_proto ${LIBS})
add_test(NAME dispatcher_test COMMAND dispatcher_test)

# -------------------------------
# 端到端测试: 服务端 + 客户端完整 RPC 链路
# -------------------------------
add_executable(end_to_end_test end_to_end_test.cc)
target_link_libraries(end_to_end_test PRIVATE core benchmark_proto ${LIBS})
add_test(NAME end_to_end_test COMMAND end_to_end_test)

# -------------------------------
# 失败路径测试: 错误码, 异常输入与失败分支覆盖
# -------------------------------
add_executable(failure_test failure_test.cc)
target_link_libraries(failure_test PRIVATE core benchmark_proto ${LIBS})
add_test(NAME failure_test COMMAND failure_test)

# -------------------------------
# 连接边界测试: 半包/粘包/边界帧处理
# -------------------------------
add_executable(connection_boundary_test connection_boundary_test.cc)
target_link_libraries(connection_boundary_test PRIVATE core benchmark_proto ${LIBS})
add_test(NAME connection_boundary_test COMMAND connection_boundary_test)

# -------------------------------
# 元数据透传测试: 请求 metadata 在服务端可见性
# -------------------------------
add_executable(metadata_test metadata_test.cc)
target_link_libraries(metadata_test PRIVATE core benchmark_proto ${LIBS})
add_test(NAME metadata_test COMMAND metadata_test)

# -------------------------------
# 消息池测试: 对象复用与复位语义
# -------------------------------
add_executable(message_pool_test message_pool_test.cc)
target_link_libraries(message_pool_test PRIVATE core benchmark_proto ${LIBS})
add_test(NAME message_pool_test COMMAND message_pool_test)

# -------------------------------
# 协程客户端测试: InvokeAsync 调用链正确性
# -------------------------------
add_executable(coroutine_client_test coroutine_client_test.cc)
target_link_libraries(coroutine_client_test PRIVATE core benchmark_proto ${LIBS})
add_test(NAME coroutine_client_test COMMAND coroutine_client_test)

# -------------------------------
# 客户端连接复用测试: 连接池复用与稳定性
# -------------------------------
add_executable(client_transport_reuse_test client_transport_reuse_test.cc)
target_link_libraries(client_transport_reuse_test PRIVATE core ${LIBS})
add_test(NAME client_transport_reuse_test COMMAND client_transport_reuse_test)
