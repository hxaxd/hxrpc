#include "rpc_server.h"

#include <stdexcept>

#include "logger.h"

namespace hxrpc {

// 构造时完成核心组件装配, 并把连接层帧回调绑定到 OnFrame
RpcServer::RpcServer(ServerConfig config)
    : config_(std::move(config)),
      reactor_(config_.reactor.max_events),
      connection_manager_(reactor_),
      serializer_(SerializerFactory::Create(config_.serialization)),
      message_pool_(std::make_shared<MessagePool>()),
      dispatcher_(registry_, serializer_, message_pool_),
      registrar_(DiscoveryFactory::CreateRegistrar(config_.discovery)) {
  connection_manager_.SetFrameHandler(
      [this](int connection_fd, std::string frame) {
        OnFrame(connection_fd, std::move(frame));
      });
}

// 业务服务注册仅写入 registry_, 真正网络暴露发生在 Run()->RegisterEndpoints()
void RpcServer::RegisterService(google::protobuf::Service* service) {
  registry_.RegisterService(service);
}

// 启动顺序说明:
// 1) 先向服务发现注册端点, 确保对外可见；
// 2) 再启动本地监听；
// 3) 最后进入 Reactor 事件循环
void RpcServer::Run() {
  RegisterEndpoints();
  if (!connection_manager_.Listen(config_.listen_endpoint)) {
    throw std::runtime_error("failed to listen on " +
                             config_.listen_endpoint.ToString());
  }
  reactor_.Loop();
}

// 停服通过通知 Reactor 退出主循环实现
void RpcServer::Stop() { reactor_.Stop(); }

// 将每个服务.方法发布到服务发现系统
void RpcServer::RegisterEndpoints() {
  for (const auto& method : registry_.ListMethods()) {
    const auto register_result = registrar_->Register(
        method.service_name, method.method_name, config_.listen_endpoint);
    if (!register_result) {
      throw std::runtime_error(register_result.error().message);
    }
  }
}

// 单帧处理:
// - 分发失败时尽力提取 request_id, 返回框架错误响应；
// - 成功时直接把编码后的响应帧写回连接
void RpcServer::OnFrame(int connection_fd, std::string frame) {
  auto response = dispatcher_.HandleFrame(frame);
  if (!response) {
    std::uint64_t request_id = 0;
    if (auto request = RpcCodec::DecodeRequest(frame); request) {
      request_id = request->request_id;
    }
    LOG(Warn) << "server request failed request_id=" << request_id
              << " code=" << static_cast<int>(response.error().code)
              << " message=" << response.error().message;
    SendFrameworkError(connection_fd, request_id, response.error());
    return;
  }
  if (auto decoded = RpcCodec::DecodeResponse(response.value()); decoded) {
    LOG(Info) << "server response request_id=" << decoded->request_id
              << " status=" << static_cast<int>(decoded->status);
  }
  connection_manager_.Send(connection_fd, std::move(response.value()));
}

// 统一构造框架错误响应并发送；编码失败时静默丢弃 (仅避免二次异常)
void RpcServer::SendFrameworkError(int connection_fd, std::uint64_t request_id,
                                   const RpcError& error) {
  RpcResponse response{request_id, error.code, error.message, {}};
  auto frame = RpcCodec::EncodeResponse(response);
  if (frame) {
    connection_manager_.Send(connection_fd, std::move(frame.value()));
  }
}

}  // namespace hxrpc
