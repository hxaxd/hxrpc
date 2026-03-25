#include "codec.h"
#include "test_support.h"
#include <arpa/inet.h>
#include <cassert>
#include <cstring>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

namespace {

int ConnectTo(std::uint16_t port) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  const int convert_result = ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  assert(convert_result == 1);
  const int connect_result =
      ::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
  assert(connect_result == 0);
  return fd;
}

void SendAll(int fd, std::string_view data) {
  std::size_t offset = 0;
  while (offset < data.size()) {
    const ssize_t written = ::send(fd, data.data() + offset, data.size() - offset, 0);
    assert(written > 0);
    offset += static_cast<std::size_t>(written);
  }
}

std::string ReceiveFrame(int fd) {
  std::uint32_t network_frame_size = 0;
  const ssize_t prefix_read = ::recv(fd, &network_frame_size, sizeof(network_frame_size),
                                     MSG_WAITALL);
  assert(prefix_read == static_cast<ssize_t>(sizeof(network_frame_size)));
  const std::uint32_t frame_size = ntohl(network_frame_size);
  std::string frame(sizeof(network_frame_size) + frame_size, '\0');
  std::memcpy(frame.data(), &network_frame_size, sizeof(network_frame_size));
  const ssize_t body_read =
      ::recv(fd, frame.data() + sizeof(network_frame_size), frame_size, MSG_WAITALL);
  assert(body_read == static_cast<ssize_t>(frame_size));
  return frame;
}

} // namespace

int main() {
  const auto port = hxrpc::test::PickFreePort();
  auto server_config = hxrpc::test::MakeServerConfig(port);
  hxrpc::RpcServer server(server_config);
  hxrpc::test::DemoUserService service;
  auto server_thread = hxrpc::test::StartServer(server, service);
  hxrpc::test::WaitForServerReady();

  const auto *login_method = hxrpc::test::FindMethod("Login");
  assert(login_method != nullptr);

  auto missing_config = hxrpc::test::MakeClientConfig(port);
  missing_config.discovery.static_services.clear();
  hxrpc::RpcClient missing_client(missing_config);
  Kuser::LoginRequest login_request;
  login_request.set_name("alice");
  login_request.set_pwd("123456");
  Kuser::LoginResponse login_response;
  const auto missing_instance =
      missing_client.Invoke(login_method, login_request, login_response);
  assert(!missing_instance.has_value());
  assert(missing_instance.error().code == hxrpc::RpcStatusCode::kDiscoveryError);

  const auto request_frame = hxrpc::RpcCodec::EncodeRequest(
      hxrpc::RpcRequest{99, "UserServiceRpc", "MissingMethod", {}, "payload"});
  assert(request_frame.has_value());
  const int unknown_method_fd = ConnectTo(port);
  SendAll(unknown_method_fd, request_frame.value());
  const auto method_response = hxrpc::RpcCodec::DecodeResponse(ReceiveFrame(unknown_method_fd));
  assert(method_response.has_value());
  assert(method_response->request_id == 99);
  assert(method_response->status == hxrpc::RpcStatusCode::kUnknownMethod);
  ::close(unknown_method_fd);

  std::string invalid_frame(sizeof(std::uint32_t), '\0');
  const std::uint32_t invalid_size = htonl(1024 * 1024 * 8);
  std::memcpy(invalid_frame.data(), &invalid_size, sizeof(invalid_size));
  const int invalid_packet_fd = ConnectTo(port);
  SendAll(invalid_packet_fd, invalid_frame);
  char byte = '\0';
  const ssize_t read_result = ::recv(invalid_packet_fd, &byte, 1, 0);
  assert(read_result <= 0);
  ::close(invalid_packet_fd);

  server.Stop();
  server_thread.join();
  return 0;
}
