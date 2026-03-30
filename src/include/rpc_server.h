#ifndef HXRPC_RPC_SERVER_H
#define HXRPC_RPC_SERVER_H

#include <memory>

#include "connection_manager.h"
#include "message_pool.h"
#include "rpc_dispatcher.h"
#include "serializer.h"
#include "service_discovery.h"
#include "service_registry.h"
#include "settings.h"

namespace hxrpc {

// RpcServer 是新的服务端门面
// 它把 Reactor, ConnectionManager, ServiceRegistry, Serializer, Dispatcher
// 装配起来, 对外提供注册服务 + 运行服务端这一组更稳定的接口
// 设计目标: 把网络收发, 方法分发, 服务发现注册封装为单入口,
// 降低上层接入复杂度
class RpcServer {
 public:
  // config: 服务端运行配置 (监听地址, 序列化策略, 发现配置, reactor 参数等)
  explicit RpcServer(ServerConfig config);

  // 注册 protobuf 业务服务通常在 Run 前完成全部注册
  void RegisterService(google::protobuf::Service* service);
  // 启动服务: 注册发现端点, 监听 socket, 进入 Reactor 主循环
  void Run();
  // 请求停止事件循环用于优雅停机触发
  void Stop();

 private:
  // 将当前 registry_ 中的方法发布到服务发现后端
  void RegisterEndpoints();
  // 处理一帧请求: 调用 dispatcher_ 生成响应并回写连接
  void OnFrame(int connection_fd, std::string frame);
  // 发送框架级错误响应 (解码失败, 分发失败等)
  void SendFrameworkError(int connection_fd, std::uint64_t request_id,
                          const RpcError& error);

  ServerConfig config_;
  Reactor reactor_;
  ConnectionManager connection_manager_;
  ServiceRegistry registry_;
  std::shared_ptr<Serializer> serializer_;
  std::shared_ptr<MessagePool> message_pool_;
  RpcDispatcher dispatcher_;
  std::unique_ptr<ServiceRegistrar> registrar_;
};

}  // namespace hxrpc

#endif
