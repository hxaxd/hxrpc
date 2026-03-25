#ifndef HXRPC_CONNECTION_MANAGER_H
#define HXRPC_CONNECTION_MANAGER_H

#include "reactor.h"
#include "types.h"
#include <functional>
#include <string>
#include <unordered_map>

namespace hxrpc {

// ConnectionManager 构建在 Reactor 之上，负责连接生命周期和字节流缓冲。
// 它把 TCP 字节流整理成“完整帧”，再把完整帧交给上层 dispatcher。
class ConnectionManager {
public:
  explicit ConnectionManager(Reactor &reactor);
  ~ConnectionManager();

  [[nodiscard]] bool Listen(const Endpoint &endpoint);
  void SetFrameHandler(std::function<void(int, std::string)> frame_handler);
  void Send(int connection_fd, std::string frame);
  void Close(int connection_fd);

private:
  static constexpr std::uint32_t kMaxFrameBodySize = 4 * 1024 * 1024;

  struct ConnectionState {
    std::string input_buffer;
    std::string output_buffer;
    std::size_t bytes_sent{0};
  };

  static bool SetNonBlocking(int fd);
  void AcceptLoop(int listen_fd);
  void HandleReadable(int connection_fd);
  void HandleWritable(int connection_fd);
  void ProcessInput(int connection_fd);
  void UpdateInterest(int connection_fd, bool want_write);

  Reactor &reactor_;
  int listen_fd_{-1};
  std::unordered_map<int, ConnectionState> connections_;
  std::function<void(int, std::string)> frame_handler_;
};

} // namespace hxrpc

#endif
