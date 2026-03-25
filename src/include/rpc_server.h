#ifndef HXRPC_RPC_SERVER_H
#define HXRPC_RPC_SERVER_H

#include "connection_manager.h"
#include "message_pool.h"
#include "rpc_dispatcher.h"
#include "serializer.h"
#include "service_discovery.h"
#include "service_registry.h"
#include "settings.h"
#include <memory>

namespace hxrpc {

// RpcServer 是新的服务端门面。
// 它把 Reactor、ConnectionManager、ServiceRegistry、Serializer、Dispatcher
// 装配起来，对外提供“注册服务 + 运行服务端”这一组更稳定的接口。
class RpcServer {
public:
  explicit RpcServer(ServerConfig config);

  void RegisterService(google::protobuf::Service *service);
  void Run();
  void Stop();

private:
  void RegisterEndpoints();
  void OnFrame(int connection_fd, std::string frame);
  void SendFrameworkError(int connection_fd, std::uint64_t request_id,
                          const RpcError &error);

  ServerConfig config_;
  Reactor reactor_;
  ConnectionManager connection_manager_;
  ServiceRegistry registry_;
  std::shared_ptr<Serializer> serializer_;
  std::shared_ptr<MessagePool> message_pool_;
  RpcDispatcher dispatcher_;
  std::unique_ptr<ServiceRegistrar> registrar_;
};

} // namespace hxrpc

#endif
