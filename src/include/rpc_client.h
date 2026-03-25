#ifndef HXRPC_RPC_CLIENT_H
#define HXRPC_RPC_CLIENT_H

#include "client_transport.h"
#include "message_pool.h"
#include "serializer.h"
#include "service_discovery.h"
#include "settings.h"
#include "task.h"
#include <google/protobuf/message.h>
#include <google/protobuf/descriptor.h>
#include <atomic>
#include <memory>

namespace hxrpc {

class RpcClient {
public:
  // RpcClient 是新的客户端门面。
  // 它不直接处理 socket 或服务发现细节，而是把 resolver、transport、
  // serializer 和 codec 串起来，形成一次完整的 RPC 调用。
  explicit RpcClient(ClientConfig config);

  [[nodiscard]] std::expected<void, RpcError>
  Invoke(const google::protobuf::MethodDescriptor *method,
         const google::protobuf::Message &request,
         google::protobuf::Message &response);
  [[nodiscard]] std::expected<void, RpcError>
  Invoke(const google::protobuf::MethodDescriptor *method,
         const google::protobuf::Message &request,
         google::protobuf::Message &response, const CallOptions &options);
  [[nodiscard]] Task<std::expected<void, RpcError>>
  InvokeAsync(const google::protobuf::MethodDescriptor *method,
              const google::protobuf::Message &request,
              google::protobuf::Message &response);
  [[nodiscard]] Task<std::expected<void, RpcError>>
  InvokeAsync(const google::protobuf::MethodDescriptor *method,
              const google::protobuf::Message &request,
              google::protobuf::Message &response, CallOptions options);

private:
  ClientConfig config_;
  std::unique_ptr<ServiceResolver> resolver_;
  std::shared_ptr<ClientTransport> transport_;
  std::shared_ptr<Serializer> serializer_;
  std::shared_ptr<MessagePool> message_pool_;
  std::atomic<std::uint64_t> next_request_id_{1};
};

} // namespace hxrpc

#endif
