#ifndef HXRPC_REACTOR_H
#define HXRPC_REACTOR_H

#include "async_runtime.h"
#include "types.h"
#include <functional>
#include <sys/epoll.h>
#include <unordered_map>
#include <vector>

namespace hxrpc {

// Reactor 是整个服务端网络层最底部的事件分发器。
// 它只关心“某个 fd 可读/可写/出错时该回调谁”，不理解连接状态、
// RPC 协议、protobuf 或业务方法。
class Reactor {
public:
  explicit Reactor(int max_events);
  ~Reactor();

  Reactor(const Reactor &) = delete;
  Reactor &operator=(const Reactor &) = delete;

  void Add(int fd, std::uint32_t events, std::function<void(int)> on_readable,
           std::function<void(int)> on_writable,
           std::function<void(int)> on_error);
  void Update(int fd, std::uint32_t events);
  void Remove(int fd);
  void Loop();
  void Stop();
  [[nodiscard]] AsyncRuntime::FdAwaiter WaitReadable(int fd, int timeout_ms);
  [[nodiscard]] AsyncRuntime::FdAwaiter WaitWritable(int fd, int timeout_ms);

private:
  struct Handler {
    std::uint32_t events{0};
    std::function<void(int)> on_readable;
    std::function<void(int)> on_writable;
    std::function<void(int)> on_error;
  };

  int epoll_fd_{-1};
  bool running_{false};
  std::vector<epoll_event> events_;
  std::unordered_map<int, Handler> handlers_;
};

} // namespace hxrpc

#endif
