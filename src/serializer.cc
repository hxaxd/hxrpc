#include "serializer.h"

namespace hxrpc {

std::expected<std::string, RpcError>
ProtobufSerializer::Serialize(const google::protobuf::Message &message,
                              const SerializationContext &context) const {
  (void)context;
  std::string payload;
  if (!message.SerializeToString(&payload)) {
    return std::unexpected(
        RpcError{RpcStatusCode::kSerializeError, "protobuf serialize failed"});
  }
  return payload;
}

std::expected<void, RpcError>
ProtobufSerializer::Deserialize(std::string_view payload,
                                google::protobuf::Message &message,
                                const SerializationContext &context) const {
  (void)context;
  if (!message.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
    return std::unexpected(
        RpcError{RpcStatusCode::kDeserializeError, "protobuf deserialize failed"});
  }
  return {};
}

std::shared_ptr<Serializer>
SerializerFactory::Create(const SerializationConfig &config) {
  (void)config;
  return std::make_shared<ProtobufSerializer>();
}

} // namespace hxrpc
