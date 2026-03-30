#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cassert>
#include <cstring>
#include <string>
#include <string_view>

#include "codec.h"
#include "serializer.h"
#include "test_support.h"

namespace {

int ConnectTo(std::uint16_t port) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  assert(fd >= 0);

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  const int convert_result =
      ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
  assert(convert_result == 1);
  const int connect_result =
      ::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));
  assert(connect_result == 0);
  return fd;
}

void SendAll(int fd, std::string_view data) {
  std::size_t offset = 0;
  while (offset < data.size()) {
    const ssize_t written =
        ::send(fd, data.data() + offset, data.size() - offset, 0);
    assert(written > 0);
    offset += static_cast<std::size_t>(written);
  }
}

std::string ReceiveFrame(int fd) {
  std::uint32_t network_frame_size = 0;
  const ssize_t prefix_read =
      ::recv(fd, &network_frame_size, sizeof(network_frame_size), MSG_WAITALL);
  assert(prefix_read == static_cast<ssize_t>(sizeof(network_frame_size)));
  const std::uint32_t frame_size = ntohl(network_frame_size);
  std::string frame(sizeof(network_frame_size) + frame_size, '\0');
  std::memcpy(frame.data(), &network_frame_size, sizeof(network_frame_size));
  const ssize_t body_read = ::recv(
      fd, frame.data() + sizeof(network_frame_size), frame_size, MSG_WAITALL);
  assert(body_read == static_cast<ssize_t>(frame_size));
  return frame;
}

std::string MakeLoginFrame(
    std::uint64_t request_id, std::string_view user, std::string_view password,
    const std::shared_ptr<hxrpc::Serializer>& serializer) {
  Kuser::LoginRequest request;
  request.set_name(std::string(user));
  request.set_pwd(std::string(password));
  auto payload = serializer->Serialize(
      request, hxrpc::SerializationContext{"UserServiceRpc", "Login"});
  assert(payload.has_value());
  auto frame = hxrpc::RpcCodec::EncodeRequest(hxrpc::RpcRequest{
      request_id, "UserServiceRpc", "Login", {}, payload.value()});
  assert(frame.has_value());
  return frame.value();
}

}  // namespace

int main() {
  // 测试目的: 覆盖连接层边界条件——半包 (拆包) 与流水线 (粘包) 处理
  const auto port = hxrpc::test::PickFreePort();
  auto server_config = hxrpc::test::MakeServerConfig(port);
  hxrpc::RpcServer server(server_config);
  hxrpc::test::DemoUserService service;
  auto server_thread = hxrpc::test::StartServer(server, service);
  hxrpc::test::WaitForServerReady();

  auto serializer =
      hxrpc::SerializerFactory::Create(hxrpc::SerializationConfig{});
  const int fd = ConnectTo(port);

  // 场景一: 人为拆分一个完整帧, 验证服务端可正确重组并返回响应
  const auto partial_frame = MakeLoginFrame(7, "alice", "123456", serializer);
  const auto split_offset = partial_frame.size() / 2;
  SendAll(fd, std::string_view(partial_frame.data(), split_offset));
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  SendAll(fd, std::string_view(partial_frame.data() + split_offset,
                               partial_frame.size() - split_offset));

  const auto partial_response =
      hxrpc::RpcCodec::DecodeResponse(ReceiveFrame(fd));
  assert(partial_response.has_value());
  assert(partial_response->request_id == 7);
  assert(partial_response->Ok());

  // 场景二: 连续发送两个请求帧, 验证服务端对同连接粘包/流水线数据的边界切分
  const auto first_frame = MakeLoginFrame(8, "alice", "123456", serializer);
  const auto second_frame =
      MakeLoginFrame(9, "alice", "bad-password", serializer);
  std::string pipelined_frames = first_frame + second_frame;
  SendAll(fd, pipelined_frames);

  const auto first_response = hxrpc::RpcCodec::DecodeResponse(ReceiveFrame(fd));
  assert(first_response.has_value());
  assert(first_response->request_id == 8);
  assert(first_response->Ok());

  const auto second_response =
      hxrpc::RpcCodec::DecodeResponse(ReceiveFrame(fd));
  assert(second_response.has_value());
  assert(second_response->request_id == 9);
  // 关键断言: 协议层处理成功业务成功与否由 payload 内业务字段表达
  assert(second_response->Ok());

  ::close(fd);
  server.Stop();
  server_thread.join();
  return 0;
}
