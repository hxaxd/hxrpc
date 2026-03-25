#ifndef HXRPC_TEST_SUPPORT_H
#define HXRPC_TEST_SUPPORT_H

#include "../benchmark/user.pb.h"
#include "rpc_client.h"
#include "rpc_server.h"
#include "serializer.h"
#include <arpa/inet.h>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <memory>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace hxrpc::test {

class DemoUserService final : public Kuser::UserServiceRpc {
public:
  void Login(::google::protobuf::RpcController *controller,
             const ::Kuser::LoginRequest *request,
             ::Kuser::LoginResponse *response,
             ::google::protobuf::Closure *done) override {
    (void)controller;
    const bool login_ok = request->name() == "alice" && request->pwd() == "123456";
    auto *result = response->mutable_result();
    result->set_errcode(login_ok ? 0 : 1);
    result->set_errmsg(login_ok ? "" : "invalid username or password");
    response->set_success(login_ok);
    done->Run();
  }

  void Register(::google::protobuf::RpcController *controller,
                const ::Kuser::RegisterRequest *request,
                ::Kuser::RegisterResponse *response,
                ::google::protobuf::Closure *done) override {
    (void)controller;
    const bool register_ok =
        request->id() > 0 && !request->name().empty() && !request->pwd().empty();
    auto *result = response->mutable_result();
    result->set_errcode(register_ok ? 0 : 1);
    result->set_errmsg(register_ok ? "" : "register request is invalid");
    response->set_success(register_ok);
    done->Run();
  }
};

inline std::uint16_t PickFreePort() {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  const int bind_result =
      ::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
  assert(bind_result == 0);

  socklen_t length = sizeof(address);
  const int name_result =
      ::getsockname(fd, reinterpret_cast<sockaddr *>(&address), &length);
  assert(name_result == 0);
  const std::uint16_t port = ntohs(address.sin_port);
  ::close(fd);
  return port;
}

inline ServerConfig MakeServerConfig(std::uint16_t port) {
  ServerConfig config;
  config.listen_endpoint = Endpoint{"127.0.0.1", port};
  config.discovery.backend = DiscoveryBackend::kStatic;
  config.serialization.backend = SerializationBackend::kProtobuf;
  config.reactor.max_events = 128;
  return config;
}

inline ClientConfig MakeClientConfig(std::uint16_t port) {
  ClientConfig config;
  config.discovery.backend = DiscoveryBackend::kStatic;
  config.discovery.static_services["UserServiceRpc.Login"] = {
      Endpoint{"127.0.0.1", port},
  };
  config.discovery.static_services["UserServiceRpc.Register"] = {
      Endpoint{"127.0.0.1", port},
  };
  config.serialization.backend = SerializationBackend::kProtobuf;
  config.call_options.timeout_ms = 1500;
  return config;
}

inline void WaitForServerReady() {
  std::this_thread::sleep_for(std::chrono::milliseconds(150));
}

inline std::thread StartServer(RpcServer &server, DemoUserService &service) {
  server.RegisterService(&service);
  return std::thread([&server]() { server.Run(); });
}

inline const google::protobuf::MethodDescriptor *
FindMethod(std::string_view method_name) {
  DemoUserService service;
  return service.GetDescriptor()->FindMethodByName(std::string(method_name));
}

} // namespace hxrpc::test

#endif
