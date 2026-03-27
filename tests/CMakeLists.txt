add_executable(config_test config_test.cc)
target_link_libraries(config_test PRIVATE core ${LIBS})
add_test(NAME config_test COMMAND config_test)

add_executable(codec_test codec_test.cc)
target_link_libraries(codec_test PRIVATE core ${LIBS})
add_test(NAME codec_test COMMAND codec_test)

add_executable(discovery_test discovery_test.cc)
target_link_libraries(discovery_test PRIVATE core ${LIBS})
add_test(NAME discovery_test COMMAND discovery_test)

add_executable(dispatcher_test dispatcher_test.cc)
target_link_libraries(dispatcher_test PRIVATE core benchmark_proto ${LIBS})
add_test(NAME dispatcher_test COMMAND dispatcher_test)

add_executable(end_to_end_test end_to_end_test.cc)
target_link_libraries(end_to_end_test PRIVATE core benchmark_proto ${LIBS})
add_test(NAME end_to_end_test COMMAND end_to_end_test)

add_executable(failure_test failure_test.cc)
target_link_libraries(failure_test PRIVATE core benchmark_proto ${LIBS})
add_test(NAME failure_test COMMAND failure_test)

add_executable(connection_boundary_test connection_boundary_test.cc)
target_link_libraries(connection_boundary_test PRIVATE core benchmark_proto ${LIBS})
add_test(NAME connection_boundary_test COMMAND connection_boundary_test)

add_executable(metadata_test metadata_test.cc)
target_link_libraries(metadata_test PRIVATE core benchmark_proto ${LIBS})
add_test(NAME metadata_test COMMAND metadata_test)

add_executable(message_pool_test message_pool_test.cc)
target_link_libraries(message_pool_test PRIVATE core benchmark_proto ${LIBS})
add_test(NAME message_pool_test COMMAND message_pool_test)

add_executable(coroutine_client_test coroutine_client_test.cc)
target_link_libraries(coroutine_client_test PRIVATE core benchmark_proto ${LIBS})
add_test(NAME coroutine_client_test COMMAND coroutine_client_test)

add_executable(client_transport_reuse_test client_transport_reuse_test.cc)
target_link_libraries(client_transport_reuse_test PRIVATE core ${LIBS})
add_test(NAME client_transport_reuse_test COMMAND client_transport_reuse_test)
