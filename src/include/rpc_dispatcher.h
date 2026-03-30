#ifndef HXRPC_DISPATCHER_H
#define HXRPC_DISPATCHER_H

#include <memory>

#include "codec.h"
#include "message_pool.h"
#include "serializer.h"
#include "service_registry.h"

namespace hxrpc {

// RpcDispatcher 负责把协议帧翻译成protobuf 方法调用, 再编码响应帧
// 它是网络层与业务 Service 之间的桥接点:
// 1) 解码请求并定位服务方法
// 2) 反序列化请求消息并调用业务方法
// 3) 序列化响应并回包
class RpcDispatcher {
 public:
  // registry: 服务/方法查找表
  // serializer: 请求/响应消息序列化实现
  // message_pool: protobuf 消息对象池, 减少频繁分配
  RpcDispatcher(const ServiceRegistry& registry,
                std::shared_ptr<Serializer> serializer,
                std::shared_ptr<MessagePool> message_pool);

  // 处理单个完整请求帧
  // 参数: frame 为按 RpcCodec 协议编码后的请求帧
  // 返回: 成功时返回已编码的响应帧失败时返回 RpcError (框架层错误)
  [[nodiscard]] std::expected<std::string, RpcError> HandleFrame(
      std::string_view frame) const;

 private:
  const ServiceRegistry& registry_;
  std::shared_ptr<Serializer> serializer_;
  std::shared_ptr<MessagePool> message_pool_;
};

}  // namespace hxrpc

#endif
