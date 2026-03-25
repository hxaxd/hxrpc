#ifndef HXRPC_SERIALIZER_H
#define HXRPC_SERIALIZER_H

#include "settings.h"
#include "types.h"
#include <expected>
#include <google/protobuf/message.h>
#include <memory>
#include <string>
#include <string_view>

namespace hxrpc {

struct SerializationContext {
  std::string service_name;
  std::string method_name;
};

// Serializer 负责“业务对象 <-> payload”之间的转换。
// RpcCodec 只处理框架头和帧边界，业务序列化由这一层决定。
class Serializer {
public:
  virtual ~Serializer() = default;
  [[nodiscard]] virtual std::expected<std::string, RpcError>
  Serialize(const google::protobuf::Message &message,
            const SerializationContext &context) const = 0;
  [[nodiscard]] virtual std::expected<void, RpcError>
  Deserialize(std::string_view payload, google::protobuf::Message &message,
              const SerializationContext &context) const = 0;
};

class ProtobufSerializer final : public Serializer {
public:
  [[nodiscard]] std::expected<std::string, RpcError>
  Serialize(const google::protobuf::Message &message,
            const SerializationContext &context) const override;
  [[nodiscard]] std::expected<void, RpcError>
  Deserialize(std::string_view payload, google::protobuf::Message &message,
              const SerializationContext &context) const override;
};

class SerializerFactory {
public:
  [[nodiscard]] static std::shared_ptr<Serializer>
  Create(const SerializationConfig &config);
};

} // namespace hxrpc

#endif
