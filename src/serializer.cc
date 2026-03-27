#include "serializer.h"

namespace hxrpc {

std::expected<std::string, RpcError> ProtobufSerializer::Serialize(
    const google::protobuf::Message& message,
    const SerializationContext& context) const {
  // 当前 protobuf 路径无需上下文参与编码逻辑；保留参数用于接口统一与未来扩展
  (void)context;
  std::string payload;
  if (!message.SerializeToString(&payload)) {
    return std::unexpected(
        RpcError{RpcStatusCode::kSerializeError, "protobuf serialize failed"});
  }
  return payload;
}

std::expected<void, RpcError> ProtobufSerializer::Deserialize(
    std::string_view payload, google::protobuf::Message& message,
    const SerializationContext& context) const {
  // 与 Serialize 对应, 反序列化目前仅依赖消息类型本身
  (void)context;
  if (!message.ParseFromArray(payload.data(),
                              static_cast<int>(payload.size()))) {
    return std::unexpected(RpcError{RpcStatusCode::kDeserializeError,
                                    "protobuf deserialize failed"});
  }
  return {};
}

std::shared_ptr<Serializer> SerializerFactory::Create(
    const SerializationConfig& config) {
  // 预留配置入口: 即使当前未使用 config, 也能保持调用方与未来多实现兼容
  (void)config;
  return std::make_shared<ProtobufSerializer>();
}

}  // namespace hxrpc
