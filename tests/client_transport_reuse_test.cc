#include "client_transport.h"
#include "codec.h"
#include <arpa/inet.h>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstring>
#include <netinet/in.h>
#include <string>
#include <string_view>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>

namespace {

std::uint16_t PickFreePort() {
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

bool RecvAll(int fd, void *buffer, std::size_t size) {
  auto *bytes = static_cast<char *>(buffer);
  std::size_t offset = 0;
  while (offset < size) {
    const ssize_t read_size = ::recv(fd, bytes + offset, size - offset, 0);
    if (read_size <= 0) {
      return false;
    }
    offset += static_cast<std::size_t>(read_size);
  }
  return true;
}

bool SendAll(int fd, std::string_view data) {
  std::size_t offset = 0;
  while (offset < data.size()) {
    const ssize_t written = ::send(fd, data.data() + offset, data.size() - offset, 0);
    if (written <= 0) {
      return false;
    }
    offset += static_cast<std::size_t>(written);
  }
  return true;
}

} // namespace

int main() {
  const auto port = PickFreePort();
  std::atomic<int> accept_count{0};
  std::atomic<int> request_count{0};
  std::atomic<bool> stop{false};

  std::thread server([&]() {
    const int listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    assert(listen_fd >= 0);

    const int reuse_addr = 1;
    (void)::setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &reuse_addr,
                       sizeof(reuse_addr));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    assert(::bind(listen_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) ==
           0);
    assert(::listen(listen_fd, 8) == 0);

    while (!stop.load()) {
      fd_set read_fds;
      FD_ZERO(&read_fds);
      FD_SET(listen_fd, &read_fds);
      timeval timeout{};
      timeout.tv_sec = 0;
      timeout.tv_usec = 100 * 1000;
      const int ready = ::select(listen_fd + 1, &read_fds, nullptr, nullptr, &timeout);
      if (ready <= 0) {
        continue;
      }

      sockaddr_in peer{};
      socklen_t peer_len = sizeof(peer);
      const int connection_fd =
          ::accept(listen_fd, reinterpret_cast<sockaddr *>(&peer), &peer_len);
      if (connection_fd < 0) {
        continue;
      }
      ++accept_count;

      timeval recv_timeout{};
      recv_timeout.tv_sec = 2;
      recv_timeout.tv_usec = 0;
      (void)::setsockopt(connection_fd, SOL_SOCKET, SO_RCVTIMEO, &recv_timeout,
                         sizeof(recv_timeout));

      while (!stop.load()) {
        std::uint32_t network_frame_size = 0;
        if (!RecvAll(connection_fd, &network_frame_size, sizeof(network_frame_size))) {
          break;
        }

        const std::uint32_t frame_size = ntohl(network_frame_size);
        std::string frame(sizeof(network_frame_size) + frame_size, '\0');
        std::memcpy(frame.data(), &network_frame_size, sizeof(network_frame_size));
        if (!RecvAll(connection_fd, frame.data() + sizeof(network_frame_size),
                     frame_size)) {
          break;
        }

        auto request = hxrpc::RpcCodec::DecodeRequest(frame);
        assert(request.has_value());
        ++request_count;

        hxrpc::RpcResponse response{request->request_id, hxrpc::RpcStatusCode::kOk, {},
                                    "ok"};
        auto encoded = hxrpc::RpcCodec::EncodeResponse(response);
        assert(encoded.has_value());
        if (!SendAll(connection_fd, encoded.value())) {
          break;
        }
      }

      ::close(connection_fd);
    }

    ::close(listen_fd);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(120));

  {
    hxrpc::TcpClientTransport transport;
    const hxrpc::Endpoint endpoint{"127.0.0.1", port};
    hxrpc::CallOptions options;
    options.timeout_ms = 1500;

    auto frame_one = hxrpc::RpcCodec::EncodeRequest(
        hxrpc::RpcRequest{1, "UserServiceRpc", "Login", {}, "hello"});
    assert(frame_one.has_value());
    auto response_one = transport.RoundTrip(endpoint, frame_one.value(), options);
    assert(response_one.has_value());
    auto decoded_one = hxrpc::RpcCodec::DecodeResponse(response_one.value());
    assert(decoded_one.has_value());
    assert(decoded_one->request_id == 1);

    auto frame_two = hxrpc::RpcCodec::EncodeRequest(
        hxrpc::RpcRequest{2, "UserServiceRpc", "Login", {}, "world"});
    assert(frame_two.has_value());
    auto response_two = transport.RoundTripAsync(endpoint, frame_two.value(), options).Get();
    assert(response_two.has_value());
    auto decoded_two = hxrpc::RpcCodec::DecodeResponse(response_two.value());
    assert(decoded_two.has_value());
    assert(decoded_two->request_id == 2);
  }

  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  stop = true;
  server.join();

  assert(request_count.load() == 2);
  assert(accept_count.load() == 1);
  return 0;
}
