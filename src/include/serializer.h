#ifndef HXRPC_SERIALIZER_H
#define HXRPC_SERIALIZER_H

#include <google/protobuf/message.h>

#include <expected>
#include <memory>
#include <string>
#include <string_view>

#include "settings.h"
#include "types.h"

namespace hxrpc {

// 序列化上下文用于向具体序列化器暴露本次调用场景
// 当前 protobuf
// 实现暂未使用这些字段, 但保留它们可支持按服务/方法做定制序列化策略
struct SerializationContext {
  // 业务服务名 (与 RpcRequest::service_name 对齐)
  std::string service_name;
  // 业务方法名 (与 RpcRequest::method_name 对齐)
  std::string method_name;
};

// Serializer 负责业务对象 <-> payload之间的转换
// RpcCodec 只处理框架头和帧边界, 业务序列化由这一层决定
class Serializer {
 public:
  virtual ~Serializer() = default;
  // 将 protobuf 消息编码为网络 payload
  // 返回值: 成功为 payload 字节串失败返回序列化错误
  [[nodiscard]] virtual std::expected<std::string, RpcError> Serialize(
      const google::protobuf::Message& message,
      const SerializationContext& context) const = 0;
  // 将 payload 解析回 protobuf 消息对象
  // message 由调用方预先构造具体类型实例失败时返回反序列化错误
  [[nodiscard]] virtual std::expected<void, RpcError> Deserialize(
      std::string_view payload, google::protobuf::Message& message,
      const SerializationContext& context) const = 0;
};

class ProtobufSerializer final : public Serializer {
 public:
  // protobuf 直通实现: 调用 Message::SerializeToString
  [[nodiscard]] std::expected<std::string, RpcError> Serialize(
      const google::protobuf::Message& message,
      const SerializationContext& context) const override;
  // protobuf 直通实现: 调用 Message::ParseFromArray
  [[nodiscard]] std::expected<void, RpcError> Deserialize(
      std::string_view payload, google::protobuf::Message& message,
      const SerializationContext& context) const override;
};

class SerializerFactory {
 public:
  // 基于配置创建序列化器实例
  // 当前仅返回 ProtobufSerializer, 后续可在此扩展 JSON/FlatBuffers 等实现
  [[nodiscard]] static std::shared_ptr<Serializer> Create(
      const SerializationConfig& config);
};

}  // namespace hxrpc

#endif
