#include "client_transport.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "async_runtime.h"

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

// 在连接复用场景下统一切换阻塞/非阻塞模式:
// 同步链路使用阻塞 I/O, 异步链路依赖 EPOLL 事件等待
bool SetSocketBlockingMode(int socket_fd, bool blocking) {
  const int flags = ::fcntl(socket_fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  const int next_flags =
      blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
  return ::fcntl(socket_fd, F_SETFL, next_flags) == 0;
}

}  // namespace

namespace hxrpc {

TcpClientTransport::~TcpClientTransport() {
  // 析构时集中回收池内连接, 避免 fd 泄漏
  std::unordered_map<std::string, std::vector<int>> pending_close;
  {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    pending_close.swap(idle_pool_);
  }
  for (auto& [_, sockets] : pending_close) {
    for (const int fd : sockets) {
      CloseSocket(fd);
    }
  }
}

std::expected<std::string, RpcError> TcpClientTransport::RoundTrip(
    const Endpoint& endpoint, std::string_view frame,
    const CallOptions& options) {
  // 关键流程: 借连接 -> 发送请求帧 -> 接收响应帧 -> 成功后归还连接
  // 任何阶段失败都直接关闭当前连接, 避免把未知状态连接放回池中
  auto connection_result = BorrowConnection(endpoint, options);
  if (!connection_result) {
    return std::unexpected(connection_result.error());
  }
  const int socket_fd = connection_result.value();

  if (auto send_result = SendAll(socket_fd, frame); !send_result) {
    CloseSocket(socket_fd);
    return std::unexpected(send_result.error());
  }
  auto frame_result = ReceiveFrame(socket_fd);
  if (!frame_result) {
    CloseSocket(socket_fd);
    return std::unexpected(frame_result.error());
  }
  ReturnConnection(endpoint, socket_fd);
  return frame_result;
}

Task<std::expected<std::string, RpcError>> TcpClientTransport::RoundTripAsync(
    const Endpoint& endpoint, std::string frame, CallOptions options) {
  // 当前实现复用同步路径, 保证同步/异步错误语义一致
  co_return RoundTrip(endpoint, frame, options);
}

std::expected<int, RpcError> TcpClientTransport::ConnectTo(
    const Endpoint& endpoint, const CallOptions& options) const {
  const int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (client_fd < 0) {
    return std::unexpected(RpcError{RpcStatusCode::kNetworkError,
                                    BuildSystemError("socket failed")});
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(endpoint.port);
  if (::inet_pton(AF_INET, endpoint.host.c_str(), &address.sin_addr) != 1) {
    ::close(client_fd);
    return std::unexpected(
        RpcError{RpcStatusCode::kNetworkError,
                 "endpoint host is not a valid IPv4 address"});
  }

  if (auto configured = ConfigureSocket(client_fd, options); !configured) {
    ::close(client_fd);
    return std::unexpected(configured.error());
  }

  if (::connect(client_fd, reinterpret_cast<sockaddr*>(&address),
                sizeof(address)) < 0) {
    ::close(client_fd);
    return std::unexpected(RpcError{RpcStatusCode::kNetworkError,
                                    BuildSystemError("connect failed")});
  }
  return client_fd;
}

Task<std::expected<int, RpcError>> TcpClientTransport::ConnectToAsync(
    const Endpoint& endpoint, const CallOptions& options) {
  const int client_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (client_fd < 0) {
    co_return std::unexpected(RpcError{RpcStatusCode::kNetworkError,
                                       BuildSystemError("socket failed")});
  }

  const int flags = ::fcntl(client_fd, F_GETFL, 0);
  if (flags < 0 || ::fcntl(client_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    ::close(client_fd);
    co_return std::unexpected(RpcError{RpcStatusCode::kNetworkError,
                                       BuildSystemError("fcntl failed")});
  }

  if (auto configured = ConfigureSocket(client_fd, options); !configured) {
    ::close(client_fd);
    co_return std::unexpected(configured.error());
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(endpoint.port);
  if (::inet_pton(AF_INET, endpoint.host.c_str(), &address.sin_addr) != 1) {
    ::close(client_fd);
    co_return std::unexpected(
        RpcError{RpcStatusCode::kNetworkError,
                 "endpoint host is not a valid IPv4 address"});
  }

  const int connect_result = ::connect(
      client_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address));

  if (connect_result == 0) {
    co_return client_fd;
  }
  if (errno != EINPROGRESS) {
    ::close(client_fd);
    co_return std::unexpected(RpcError{RpcStatusCode::kNetworkError,
                                       BuildSystemError("connect failed")});
  }

  const bool ready = co_await AsyncRuntime::Instance().WaitFor(
      client_fd, EPOLLOUT, options.timeout_ms);
  if (!ready) {
    ::close(client_fd);
    co_return std::unexpected(
        RpcError{RpcStatusCode::kNetworkError, "connect timed out"});
  }

  int socket_error = 0;
  socklen_t error_length = sizeof(socket_error);
  if (::getsockopt(client_fd, SOL_SOCKET, SO_ERROR, &socket_error,
                   &error_length) < 0 ||
      socket_error != 0) {
    if (socket_error != 0) {
      errno = socket_error;
    }
    ::close(client_fd);
    co_return std::unexpected(RpcError{RpcStatusCode::kNetworkError,
                                       BuildSystemError("connect failed")});
  }

  co_return client_fd;
}

std::expected<void, RpcError> TcpClientTransport::SendAll(
    int socket_fd, std::string_view data) {
  std::size_t offset = 0;
  while (offset < data.size()) {
    const ssize_t written =
        ::send(socket_fd, data.data() + offset, data.size() - offset, 0);
    if (written == 0) {
      return std::unexpected(
          RpcError{RpcStatusCode::kNetworkError, "send returned zero bytes"});
    }
    if (written < 0) {
      if (errno == EINTR) {
        continue;
      }
      return std::unexpected(RpcError{RpcStatusCode::kNetworkError,
                                      BuildSystemError("send failed")});
    }
    offset += static_cast<std::size_t>(written);
  }
  return {};
}

Task<std::expected<void, RpcError>> TcpClientTransport::SendAllAsync(
    int socket_fd, std::string_view data, int timeout_ms) {
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
      const bool writable = co_await AsyncRuntime::Instance().WaitFor(
          socket_fd, EPOLLOUT, timeout_ms);
      if (!writable) {
        co_return std::unexpected(
            RpcError{RpcStatusCode::kNetworkError, "send timed out"});
      }
      continue;
    }
    co_return std::unexpected(RpcError{RpcStatusCode::kNetworkError,
                                       BuildSystemError("send failed")});
  }
  co_return std::expected<void, RpcError>{};
}

std::expected<void, RpcError> TcpClientTransport::RecvAll(int socket_fd,
                                                          void* buffer,
                                                          std::size_t size) {
  auto* bytes = static_cast<char*>(buffer);
  std::size_t offset = 0;
  while (offset < size) {
    const ssize_t read_size =
        ::recv(socket_fd, bytes + offset, size - offset, 0);
    if (read_size == 0) {
      return std::unexpected(
          RpcError{RpcStatusCode::kNetworkError, "peer closed connection"});
    }
    if (read_size < 0) {
      if (errno == EINTR) {
        continue;
      }
      return std::unexpected(RpcError{RpcStatusCode::kNetworkError,
                                      BuildSystemError("recv failed")});
    }
    offset += static_cast<std::size_t>(read_size);
  }
  return {};
}

Task<std::expected<void, RpcError>> TcpClientTransport::RecvAllAsync(
    int socket_fd, void* buffer, std::size_t size, int timeout_ms) {
  auto* bytes = static_cast<char*>(buffer);
  std::size_t offset = 0;
  while (offset < size) {
    const ssize_t read_size =
        ::recv(socket_fd, bytes + offset, size - offset, 0);
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
      const bool readable = co_await AsyncRuntime::Instance().WaitFor(
          socket_fd, EPOLLIN, timeout_ms);
      if (!readable) {
        co_return std::unexpected(
            RpcError{RpcStatusCode::kNetworkError, "recv timed out"});
      }
      continue;
    }
    co_return std::unexpected(RpcError{RpcStatusCode::kNetworkError,
                                       BuildSystemError("recv failed")});
  }
  co_return std::expected<void, RpcError>{};
}

std::expected<std::string, RpcError> TcpClientTransport::ReceiveFrame(
    int socket_fd) const {
  // 帧格式: 4 字节网络序长度 + body先收长度再按长度收 body
  std::uint32_t network_frame_size = 0;
  if (auto recv_result =
          RecvAll(socket_fd, &network_frame_size, sizeof(network_frame_size));
      !recv_result) {
    return std::unexpected(recv_result.error());
  }
  const std::uint32_t frame_size = ntohl(network_frame_size);
  // 下限: 长度字段至少覆盖 body 长度语义上限: 防止异常大包导致内存风险
  constexpr std::uint32_t kMaxFrameSize = 4 * 1024 * 1024;
  if (frame_size < sizeof(network_frame_size) || frame_size > kMaxFrameSize) {
    return std::unexpected(RpcError{RpcStatusCode::kInvalidPacket,
                                    "response frame length is invalid"});
  }
  std::string frame(sizeof(network_frame_size) + frame_size, '\0');
  std::memcpy(frame.data(), &network_frame_size, sizeof(network_frame_size));
  if (auto recv_result = RecvAll(
          socket_fd, frame.data() + sizeof(network_frame_size), frame_size);
      !recv_result) {
    return std::unexpected(recv_result.error());
  }
  return frame;
}

Task<std::expected<std::string, RpcError>>
TcpClientTransport::ReceiveFrameAsync(int socket_fd, int timeout_ms) {
  // 异步版本保持与同步版相同的帧校验规则
  std::uint32_t network_frame_size = 0;
  auto recv_prefix_task = RecvAllAsync(socket_fd, &network_frame_size,
                                       sizeof(network_frame_size), timeout_ms);
  if (auto recv_result = co_await recv_prefix_task; !recv_result) {
    co_return std::unexpected(recv_result.error());
  }
  const std::uint32_t frame_size = ntohl(network_frame_size);
  constexpr std::uint32_t kMaxFrameSize = 4 * 1024 * 1024;
  if (frame_size < sizeof(network_frame_size) || frame_size > kMaxFrameSize) {
    co_return std::unexpected(RpcError{RpcStatusCode::kInvalidPacket,
                                       "response frame length is invalid"});
  }
  std::string frame(sizeof(network_frame_size) + frame_size, '\0');
  std::memcpy(frame.data(), &network_frame_size, sizeof(network_frame_size));
  auto recv_body_task =
      RecvAllAsync(socket_fd, frame.data() + sizeof(network_frame_size),
                   frame_size, timeout_ms);
  if (auto recv_result = co_await recv_body_task; !recv_result) {
    co_return std::unexpected(recv_result.error());
  }
  co_return frame;
}

std::shared_ptr<ClientTransport> ClientTransportFactory::Create() {
  return std::make_shared<TcpClientTransport>();
}

std::string TcpClientTransport::EndpointKey(const Endpoint& endpoint) {
  return endpoint.ToString();
}

std::expected<void, RpcError> TcpClientTransport::ConfigureSocket(
    int socket_fd, const CallOptions& options) const {
  // 使用 SO_RCVTIMEO/SO_SNDTIMEO 统一套接字超时, 防止阻塞无限等待
  timeval timeout{};
  timeout.tv_sec = options.timeout_ms / 1000;
  timeout.tv_usec = (options.timeout_ms % 1000) * 1000;
  if (::setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                   sizeof(timeout)) < 0) {
    return std::unexpected(
        RpcError{RpcStatusCode::kNetworkError,
                 BuildSystemError("setsockopt(RCVTIMEO) failed")});
  }
  if (::setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                   sizeof(timeout)) < 0) {
    return std::unexpected(
        RpcError{RpcStatusCode::kNetworkError,
                 BuildSystemError("setsockopt(SNDTIMEO) failed")});
  }
  return {};
}

std::expected<int, RpcError> TcpClientTransport::BorrowConnection(
    const Endpoint& endpoint, const CallOptions& options) {
  const auto key = EndpointKey(endpoint);
  {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    // 先尝试复用空闲连接, 失败再降级为新建连接
    auto it = idle_pool_.find(key);
    if (it != idle_pool_.end() && !it->second.empty()) {
      const int socket_fd = it->second.back();
      it->second.pop_back();
      if (it->second.empty()) {
        idle_pool_.erase(it);
      }
      if (!SetSocketBlockingMode(socket_fd, true)) {
        CloseSocket(socket_fd);
        return ConnectTo(endpoint, options);
      }
      if (auto configured = ConfigureSocket(socket_fd, options); configured) {
        return socket_fd;
      }
      CloseSocket(socket_fd);
    }
  }
  return ConnectTo(endpoint, options);
}

Task<std::expected<int, RpcError>> TcpClientTransport::BorrowConnectionAsync(
    const Endpoint& endpoint, const CallOptions& options) {
  const auto key = EndpointKey(endpoint);
  {
    std::lock_guard<std::mutex> lock(pool_mutex_);
    // 异步路径复用连接时需切到非阻塞模式, 配合事件循环等待可读/可写
    auto it = idle_pool_.find(key);
    if (it != idle_pool_.end() && !it->second.empty()) {
      const int socket_fd = it->second.back();
      it->second.pop_back();
      if (it->second.empty()) {
        idle_pool_.erase(it);
      }
      if (!SetSocketBlockingMode(socket_fd, false)) {
        CloseSocket(socket_fd);
        auto fallback_connect_task = ConnectToAsync(endpoint, options);
        auto fallback_connect_result = co_await fallback_connect_task;
        co_return fallback_connect_result;
      }
      if (auto configured = ConfigureSocket(socket_fd, options); configured) {
        co_return socket_fd;
      }
      CloseSocket(socket_fd);
    }
  }
  auto connect_task = ConnectToAsync(endpoint, options);
  auto connect_result = co_await connect_task;
  co_return connect_result;
}

void TcpClientTransport::ReturnConnection(const Endpoint& endpoint,
                                          int socket_fd) {
  const auto key = EndpointKey(endpoint);
  std::lock_guard<std::mutex> lock(pool_mutex_);
  auto& bucket = idle_pool_[key];
  // 超过池容量直接关闭, 避免连接池无限膨胀
  if (bucket.size() >= kMaxIdlePerEndpoint) {
    CloseSocket(socket_fd);
    return;
  }
  bucket.push_back(socket_fd);
}

void TcpClientTransport::CloseSocket(int socket_fd) {
  if (socket_fd >= 0) {
    ::close(socket_fd);
  }
}

}  // namespace hxrpc
