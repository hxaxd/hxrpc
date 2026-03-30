#ifndef HXRPC_REACTOR_H
#define HXRPC_REACTOR_H

#include <sys/epoll.h>

#include <functional>
#include <unordered_map>
#include <vector>

#include "async_runtime.h"
#include "types.h"

namespace hxrpc {

// Reactor 是整个服务端网络层最底部的事件分发器
// 它只关心某个 fd 可读/可写/出错时该回调谁, 不理解连接状态,
// RPC 协议, protobuf 或业务方法
// 设计上保持极简:
// 1) 统一管理 epoll 与 fd->回调映射
// 2) 与上层通过回调解耦, 避免网络事件循环与 RPC 语义耦合
// 3) 向协程运行时暴露按 fd/事件等待的能力
class Reactor {
 public:
  // max_events: 单次 epoll_wait 可返回的最大事件数, 用于初始化事件数组容量
  explicit Reactor(int max_events);
  ~Reactor();

  Reactor(const Reactor&) = delete;
  Reactor& operator=(const Reactor&) = delete;

  // 注册 fd 及其三类回调 (可读/可写/错误)
  // events 通常为 EPOLLIN 或 EPOLLIN|EPOLLOUT
  void Add(int fd, std::uint32_t events, std::function<void(int)> on_readable,
           std::function<void(int)> on_writable,
           std::function<void(int)> on_error);
  // 更新 fd 关注的事件集合, 不改变回调函数
  void Update(int fd, std::uint32_t events);
  // 从 epoll 与内部映射移除 fd调用方负责业务层状态清理
  void Remove(int fd);
  // 主事件循环: 阻塞等待 I/O 事件并分发给对应回调
  void Loop();
  // 请求退出事件循环由 Loop 在下一次轮询时感知并返回
  void Stop();
  // 为协程场景提供等待可读能力
  // timeout_ms < 0 通常表示不超时 (由 AsyncRuntime 解释具体语义)
  [[nodiscard]] AsyncRuntime::FdAwaiter WaitReadable(int fd, int timeout_ms);
  // 为协程场景提供等待可写能力
  [[nodiscard]] AsyncRuntime::FdAwaiter WaitWritable(int fd, int timeout_ms);

 private:
  // 单个 fd 的事件关注集与回调集合
  struct Handler {
    std::uint32_t events{0};
    std::function<void(int)> on_readable;
    std::function<void(int)> on_writable;
    std::function<void(int)> on_error;
  };

  // epoll 实例句柄
  int epoll_fd_{-1};
  // 事件循环运行标志
  bool running_{false};
  // epoll_wait 输出缓冲区, 容量由构造参数决定
  std::vector<epoll_event> events_;
  // fd 到处理器的映射, 事件到来时据此分发回调
  std::unordered_map<int, Handler> handlers_;
};

}  // namespace hxrpc

#endif
