#ifndef HXRPC_RPC_CLIENT_H
#define HXRPC_RPC_CLIENT_H

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>

#include <atomic>
#include <memory>

#include "client_transport.h"
#include "message_pool.h"
#include "serializer.h"
#include "service_discovery.h"
#include "settings.h"
#include "task.h"

namespace hxrpc {

class RpcClient {
 public:
  // RpcClient 是客户端调用门面:
  // 负责串联服务发现, 请求序列化, 帧编解码, 网络传输与响应反序列化
  // 设计目标: 将业务调用与底层通信细节解耦
  explicit RpcClient(ClientConfig config);

  // 同步调用接口
  // method/request/response: 对应 protobuf 方法描述, 请求与输出响应对象
  // 返回: 成功为 void失败为 RpcError (发现失败, 网络失败, 编解码失败等)
  [[nodiscard]] std::expected<void, RpcError> Invoke(
      const google::protobuf::MethodDescriptor* method,
      const google::protobuf::Message& request,
      google::protobuf::Message& response);
  // 使用调用方显式 options 覆盖默认调用参数 (如超时/metadata)
  [[nodiscard]] std::expected<void, RpcError> Invoke(
      const google::protobuf::MethodDescriptor* method,
      const google::protobuf::Message& request,
      google::protobuf::Message& response, const CallOptions& options);
  // 异步调用接口, 语义与同步接口一致
  [[nodiscard]] Task<std::expected<void, RpcError>> InvokeAsync(
      const google::protobuf::MethodDescriptor* method,
      const google::protobuf::Message& request,
      google::protobuf::Message& response);
  [[nodiscard]] Task<std::expected<void, RpcError>> InvokeAsync(
      const google::protobuf::MethodDescriptor* method,
      const google::protobuf::Message& request,
      google::protobuf::Message& response, CallOptions options);

 private:
  ClientConfig config_;
  // 组件拆分: 分别负责实例解析, 传输, 序列化与消息对象复用
  std::unique_ptr<ServiceResolver> resolver_;
  std::shared_ptr<ClientTransport> transport_;
  std::shared_ptr<Serializer> serializer_;
  std::shared_ptr<MessagePool> message_pool_;
  // 单调递增请求号, 用于日志关联与链路排查
  std::atomic<std::uint64_t> next_request_id_{1};
};

}  // namespace hxrpc

#endif
