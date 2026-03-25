#include "client_transport.h"
#include "async_runtime.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

class ScopedFd {
public:
  explicit ScopedFd(int fd) : fd_(fd) {}
  ~ScopedFd() {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }

private:
  int fd_;
};

std::string BuildSystemError(std::string_view prefix) {
  return std::string(prefix) + ": " + std::strerror(errno);
}

} // namespace

namespace hxrpc {

std::expected<std::string, RpcError>
TcpClientTransport::RoundTrip(const Endpoint &endpoint, std::string_view frame,
                              const CallOptions &options) {
  auto connect_result = ConnectTo(endpoint, options);
  if (!connect_result) {
    return std::unexpected(connect_result.error());
  }
  ScopedFd fd(connect_result.value());
  if (auto send_result = SendAll(connect_result.value(), frame); !send_result) {
    return std::unexpected(send_result.error());
  }
  return ReceiveFrame(connect_result.value());
}

Task<std::expected<std::string, RpcError>>
TcpClientTransport::RoundTripAsync(const Endpoint &endpoint, std::string frame,
                                   CallOptions options) {
  co_return RoundTrip(endpoint, frame, options);
}

std::expected<int, RpcError>
TcpClientTransport::ConnectTo(const Endpoint &endpoint,
                              const CallOptions &options) const {
  const int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (client_fd < 0) {
    return std::unexpected(
        RpcError{RpcStatusCode::kNetworkError, BuildSystemError("socket failed")});
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(endpoint.port);
  if (::inet_pton(AF_INET, endpoint.host.c_str(), &address.sin_addr) != 1) {
    ::close(client_fd);
    return std::unexpected(
        RpcError{RpcStatusCode::kNetworkError, "endpoint host is not a valid IPv4 address"});
  }

  timeval timeout{};
  timeout.tv_sec = options.timeout_ms / 1000;
  timeout.tv_usec = (options.timeout_ms % 1000) * 1000;
  (void)::setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  (void)::setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

  if (::connect(client_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
    ::close(client_fd);
    return std::unexpected(
        RpcError{RpcStatusCode::kNetworkError, BuildSystemError("connect failed")});
  }
  return client_fd;
}

Task<std::expected<int, RpcError>>
TcpClientTransport::ConnectToAsync(const Endpoint &endpoint,
                                   const CallOptions &options) {
  const int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (client_fd < 0) {
    co_return std::unexpected(
        RpcError{RpcStatusCode::kNetworkError, BuildSystemError("socket failed")});
  }

  const int flags = ::fcntl(client_fd, F_GETFL, 0);
  if (flags < 0 || ::fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    ::close(client_fd);
    co_return std::unexpected(
        RpcError{RpcStatusCode::kNetworkError, BuildSystemError("fcntl failed")});
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(endpoint.port);
  if (::inet_pton(AF_INET, endpoint.host.c_str(), &address.sin_addr) != 1) {
    ::close(client_fd);
    co_return std::unexpected(
        RpcError{RpcStatusCode::kNetworkError, "endpoint host is not a valid IPv4 address"});
  }

  const int connect_result =
      ::connect(client_fd, reinterpret_cast<sockaddr *>(&address), sizeof(address));
  if (connect_result == 0) {
    co_return client_fd;
  }
  if (errno != EINPROGRESS) {
    ::close(client_fd);
    co_return std::unexpected(
        RpcError{RpcStatusCode::kNetworkError, BuildSystemError("connect failed")});
  }

  const bool ready =
      co_await AsyncRuntime::Instance().WaitFor(client_fd, EPOLLOUT, options.timeout_ms);
  if (!ready) {
    ::close(client_fd);
    co_return std::unexpected(
        RpcError{RpcStatusCode::kNetworkError, "connect timed out"});
  }

  int socket_error = 0;
  socklen_t error_length = sizeof(socket_error);
  if (::getsockopt(client_fd, SOL_SOCKET, SO_ERROR, &socket_error, &error_length) < 0 ||
      socket_error != 0) {
    if (socket_error != 0) {
      errno = socket_error;
    }
    ::close(client_fd);
    co_return std::unexpected(
        RpcError{RpcStatusCode::kNetworkError, BuildSystemError("connect failed")});
  }

  co_return client_fd;
}

std::expected<void, RpcError>
TcpClientTransport::SendAll(int socket_fd, std::string_view data) {
  std::size_t offset = 0;
  while (offset < data.size()) {
    const ssize_t written = ::send(socket_fd, data.data() + offset, data.size() - offset, 0);
    if (written == 0) {
      return std::unexpected(
          RpcError{RpcStatusCode::kNetworkError, "send returned zero bytes"});
    }
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return std::unexpected(
          RpcError{RpcStatusCode::kNetworkError, BuildSystemError("send failed")});
    }
    offset += static_cast<std::size_t>(written);
  }
  return {};
}

Task<std::expected<void, RpcError>>
TcpClientTransport::SendAllAsync(int socket_fd, std::string_view data, int timeout_ms) {
  std::size_t offset = 0;
  while (offset < data.size()) {
    const ssize_t written =
        ::send(socket_fd, data.data() + offset, data.size() - offset, 0);
    if (written > 0) {
      offset += static_cast<std::size_t>(written);
      continue;
    }
    if (written == 0) {
      co_return std::unexpected(
          RpcError{RpcStatusCode::kNetworkError, "send returned zero bytes"});
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      const bool writable =
          co_await AsyncRuntime::Instance().WaitFor(socket_fd, EPOLLOUT, timeout_ms);
      if (!writable) {
        co_return std::unexpected(
            RpcError{RpcStatusCode::kNetworkError, "send timed out"});
      }
      continue;
    }
    co_return std::unexpected(
        RpcError{RpcStatusCode::kNetworkError, BuildSystemError("send failed")});
  }
  co_return std::expected<void, RpcError>{};
}

std::expected<void, RpcError>
TcpClientTransport::RecvAll(int socket_fd, void *buffer, std::size_t size) {
  auto *bytes = static_cast<char *>(buffer);
  std::size_t offset = 0;
  while (offset < size) {
    const ssize_t read_size = ::recv(socket_fd, bytes + offset, size - offset, 0);
    if (read_size == 0) {
      return std::unexpected(
          RpcError{RpcStatusCode::kNetworkError, "peer closed connection"});
    }
    if (read_size < 0) {
      if (errno == EINTR) {
        continue;
      }
      return std::unexpected(
          RpcError{RpcStatusCode::kNetworkError, BuildSystemError("recv failed")});
    }
    offset += static_cast<std::size_t>(read_size);
  }
  return {};
}

Task<std::expected<void, RpcError>>
TcpClientTransport::RecvAllAsync(int socket_fd, void *buffer, std::size_t size,
                                 int timeout_ms) {
  auto *bytes = static_cast<char *>(buffer);
  std::size_t offset = 0;
  while (offset < size) {
    const ssize_t read_size = ::recv(socket_fd, bytes + offset, size - offset, 0);
    if (read_size > 0) {
      offset += static_cast<std::size_t>(read_size);
      continue;
    }
    if (read_size == 0) {
      co_return std::unexpected(
          RpcError{RpcStatusCode::kNetworkError, "peer closed connection"});
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      const bool readable =
          co_await AsyncRuntime::Instance().WaitFor(socket_fd, EPOLLIN, timeout_ms);
      if (!readable) {
        co_return std::unexpected(
            RpcError{RpcStatusCode::kNetworkError, "recv timed out"});
      }
      continue;
    }
    co_return std::unexpected(
        RpcError{RpcStatusCode::kNetworkError, BuildSystemError("recv failed")});
  }
  co_return std::expected<void, RpcError>{};
}

std::expected<std::string, RpcError>
TcpClientTransport::ReceiveFrame(int socket_fd) const {
  std::uint32_t network_frame_size = 0;
  if (auto recv_result = RecvAll(socket_fd, &network_frame_size, sizeof(network_frame_size));
      !recv_result) {
    return std::unexpected(recv_result.error());
  }
  const std::uint32_t frame_size = ntohl(network_frame_size);
  constexpr std::uint32_t kMaxFrameSize = 4 * 1024 * 1024;
  if (frame_size < sizeof(network_frame_size) || frame_size > kMaxFrameSize) {
    return std::unexpected(
        RpcError{RpcStatusCode::kInvalidPacket, "response frame length is invalid"});
  }
  std::string frame(sizeof(network_frame_size) + frame_size, '\0');
  std::memcpy(frame.data(), &network_frame_size, sizeof(network_frame_size));
  if (auto recv_result =
          RecvAll(socket_fd, frame.data() + sizeof(network_frame_size), frame_size);
      !recv_result) {
    return std::unexpected(recv_result.error());
  }
  return frame;
}

Task<std::expected<std::string, RpcError>>
TcpClientTransport::ReceiveFrameAsync(int socket_fd, int timeout_ms) {
  std::uint32_t network_frame_size = 0;
  if (auto recv_result =
          co_await RecvAllAsync(socket_fd, &network_frame_size, sizeof(network_frame_size),
                                timeout_ms);
      !recv_result) {
    co_return std::unexpected(recv_result.error());
  }
  const std::uint32_t frame_size = ntohl(network_frame_size);
  constexpr std::uint32_t kMaxFrameSize = 4 * 1024 * 1024;
  if (frame_size < sizeof(network_frame_size) || frame_size > kMaxFrameSize) {
    co_return std::unexpected(
        RpcError{RpcStatusCode::kInvalidPacket, "response frame length is invalid"});
  }
  std::string frame(sizeof(network_frame_size) + frame_size, '\0');
  std::memcpy(frame.data(), &network_frame_size, sizeof(network_frame_size));
  if (auto recv_result = co_await RecvAllAsync(
          socket_fd, frame.data() + sizeof(network_frame_size), frame_size,
          timeout_ms);
      !recv_result) {
    co_return std::unexpected(recv_result.error());
  }
  co_return frame;
}

std::shared_ptr<ClientTransport> ClientTransportFactory::Create() {
  return std::make_shared<TcpClientTransport>();
}

} // namespace hxrpc
