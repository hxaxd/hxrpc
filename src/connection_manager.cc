#include "connection_manager.h"
#include "logger.h"
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace hxrpc {

ConnectionManager::ConnectionManager(Reactor &reactor) : reactor_(reactor) {}

ConnectionManager::~ConnectionManager() {
  for (auto &[fd, state] : connections_) {
    (void)state;
    ::close(fd);
  }
  if (listen_fd_ >= 0) {
    ::close(listen_fd_);
  }
}

bool ConnectionManager::Listen(const Endpoint &endpoint) {
  listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd_ < 0) {
    LOG(Error) << "socket failed: " << std::strerror(errno);
    return false;
  }
  const int reuse_addr = 1;
  if (::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse_addr,
                   sizeof(reuse_addr)) < 0) {
    LOG(Warn) << "setsockopt(SO_REUSEADDR) failed: " << std::strerror(errno);
  }
  if (!SetNonBlocking(listen_fd_)) {
    LOG(Error) << "fcntl(O_NONBLOCK) failed for listen fd: " << std::strerror(errno);
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(endpoint.port);
  if (::inet_pton(AF_INET, endpoint.host.c_str(), &address.sin_addr) != 1) {
    LOG(Error) << "listen endpoint host is not a valid IPv4 address: " << endpoint.host;
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  if (::bind(listen_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) < 0) {
    LOG(Error) << "bind failed for " << endpoint.ToString() << ": "
               << std::strerror(errno);
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }
  if (::listen(listen_fd_, SOMAXCONN) < 0) {
    LOG(Error) << "listen failed for " << endpoint.ToString() << ": "
               << std::strerror(errno);
    ::close(listen_fd_);
    listen_fd_ = -1;
    return false;
  }

  reactor_.Add(listen_fd_, EPOLLIN,
               [this](int fd) { AcceptLoop(fd); }, {},
               [this](int fd) { Close(fd); });
  return true;
}

void ConnectionManager::SetFrameHandler(
    std::function<void(int, std::string)> frame_handler) {
  frame_handler_ = std::move(frame_handler);
}

void ConnectionManager::Send(int connection_fd, std::string frame) {
  auto it = connections_.find(connection_fd);
  if (it == connections_.end()) {
    return;
  }
  it->second.output_buffer.append(std::move(frame));
  UpdateInterest(connection_fd, true);
  HandleWritable(connection_fd);
}

void ConnectionManager::Close(int connection_fd) {
  if (connection_fd == listen_fd_) {
    reactor_.Remove(connection_fd);
    ::close(connection_fd);
    listen_fd_ = -1;
    return;
  }

  auto it = connections_.find(connection_fd);
  if (it == connections_.end()) {
    return;
  }
  reactor_.Remove(connection_fd);
  ::close(connection_fd);
  connections_.erase(it);
}

bool ConnectionManager::SetNonBlocking(int fd) {
  const int flags = ::fcntl(fd, F_GETFL, 0);
  if (flags < 0) {
    return false;
  }
  return ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}

void ConnectionManager::AcceptLoop(int listen_fd) {
  while (true) {
    sockaddr_in peer{};
    socklen_t peer_len = sizeof(peer);
    const int connection_fd =
        ::accept(listen_fd, reinterpret_cast<sockaddr *>(&peer), &peer_len);
    if (connection_fd < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return;
      }
      if (errno == EINTR) {
        continue;
      }
      return;
    }

    if (!SetNonBlocking(connection_fd)) {
      LOG(Warn) << "failed to set connection nonblocking: " << std::strerror(errno);
      ::close(connection_fd);
      continue;
    }
    connections_.emplace(connection_fd, ConnectionState{});
    reactor_.Add(connection_fd, EPOLLIN,
                 [this](int fd) { HandleReadable(fd); },
                 [this](int fd) { HandleWritable(fd); },
                 [this](int fd) { Close(fd); });
  }
}

void ConnectionManager::HandleReadable(int connection_fd) {
  auto it = connections_.find(connection_fd);
  if (it == connections_.end()) {
    return;
  }

  char buffer[4096];
  while (true) {
    const ssize_t bytes_read = ::recv(connection_fd, buffer, sizeof(buffer), 0);
    if (bytes_read > 0) {
      it->second.input_buffer.append(buffer, static_cast<std::size_t>(bytes_read));
      continue;
    }
    if (bytes_read == 0) {
      Close(connection_fd);
      return;
    }
    if (errno == EINTR) {
      continue;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    }
    Close(connection_fd);
    return;
  }
  ProcessInput(connection_fd);
}

void ConnectionManager::HandleWritable(int connection_fd) {
  auto it = connections_.find(connection_fd);
  if (it == connections_.end()) {
    return;
  }

  auto &state = it->second;
  while (state.bytes_sent < state.output_buffer.size()) {
    const ssize_t written =
        ::send(connection_fd, state.output_buffer.data() + state.bytes_sent,
               state.output_buffer.size() - state.bytes_sent, 0);
    if (written > 0) {
      state.bytes_sent += static_cast<std::size_t>(written);
      continue;
    }
    if (written < 0 && errno == EINTR) {
      continue;
    }
    if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      UpdateInterest(connection_fd, true);
      return;
    }
    Close(connection_fd);
    return;
  }

  state.output_buffer.clear();
  state.bytes_sent = 0;
  UpdateInterest(connection_fd, false);
}

void ConnectionManager::ProcessInput(int connection_fd) {
  auto it = connections_.find(connection_fd);
  if (it == connections_.end()) {
    return;
  }

  auto &input = it->second.input_buffer;
  while (input.size() >= sizeof(std::uint32_t)) {
    std::uint32_t network_frame_size = 0;
    std::memcpy(&network_frame_size, input.data(), sizeof(network_frame_size));
    const std::uint32_t frame_size = ntohl(network_frame_size);
    if (frame_size < sizeof(std::uint32_t) || frame_size > kMaxFrameBodySize) {
      LOG(Warn) << "closing connection due to invalid frame length " << frame_size;
      Close(connection_fd);
      return;
    }
    const std::size_t total_size = sizeof(std::uint32_t) + frame_size;
    if (input.size() < total_size) {
      break;
    }

    std::string frame = input.substr(0, total_size);
    input.erase(0, total_size);
    if (frame_handler_) {
      frame_handler_(connection_fd, std::move(frame));
    }
  }
}

void ConnectionManager::UpdateInterest(int connection_fd, bool want_write) {
  reactor_.Update(connection_fd, want_write ? (EPOLLIN | EPOLLOUT) : EPOLLIN);
}

} // namespace hxrpc
