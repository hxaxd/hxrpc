file(GLOB SRC_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.cc)

find_package(Protobuf REQUIRED)

add_library(core STATIC ${SRC_FILES})

set(PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/header.proto)
protobuf_generate(TARGET core LANGUAGE cpp PROTOS ${PROTO_FILES})

target_include_directories(core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(core PUBLIC protobuf)
target_compile_features(core PUBLIC cxx_std_23)
target_compile_options(core PRIVATE -Wall -Wextra -Wpedantic)
