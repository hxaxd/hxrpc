#include "rpc_server.h"
#include "logger.h"
#include <stdexcept>

namespace hxrpc {

RpcServer::RpcServer(ServerConfig config)
    : config_(std::move(config)), reactor_(config_.reactor.max_events),
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

void RpcServer::RegisterService(google::protobuf::Service *service) {
  registry_.RegisterService(service);
}

void RpcServer::Run() {
  RegisterEndpoints();
  if (!connection_manager_.Listen(config_.listen_endpoint)) {
    throw std::runtime_error("failed to listen on " + config_.listen_endpoint.ToString());
  }
  reactor_.Loop();
}

void RpcServer::Stop() { reactor_.Stop(); }

void RpcServer::RegisterEndpoints() {
  for (const auto &method : registry_.ListMethods()) {
    const auto register_result =
        registrar_->Register(method.service_name, method.method_name,
                             config_.listen_endpoint);
    if (!register_result) {
      throw std::runtime_error(register_result.error().message);
    }
  }
}

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

void RpcServer::SendFrameworkError(int connection_fd, std::uint64_t request_id,
                                   const RpcError &error) {
  RpcResponse response{request_id, error.code, error.message, {}};
  auto frame = RpcCodec::EncodeResponse(response);
  if (frame) {
    connection_manager_.Send(connection_fd, std::move(frame.value()));
  }
}

} // namespace hxrpc
