#ifndef HXRPC_ASYNC_RUNTIME_H
#define HXRPC_ASYNC_RUNTIME_H

// src/include/async_runtime.h
// 协程异步运行时接口
// 职责: 把 fd 就绪事件与超时事件桥接到 C++20 协程, 提供可取消的 awaiter

#include <sys/epoll.h>

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

namespace hxrpc {

class AsyncRuntime {
 public:
  // 获取运行时单例
  // 返回: 全局 AsyncRuntime 引用
  static AsyncRuntime& Instance();

  AsyncRuntime(const AsyncRuntime&) = delete;
  AsyncRuntime& operator=(const AsyncRuntime&) = delete;

  class FdAwaiter {
   public:
    // 构造 fd awaiter
    // 参数:
    //   - fd: 监听的文件描述符；
    //   - events: epoll 事件掩码 (例如 EPOLLIN/EPOLLOUT) ；
    //   - timeout_ms: 超时毫秒数
    // 错误语义: 构造阶段不进行系统调用, 注册失败在 await_suspend
    // 后由运行时处理
    FdAwaiter(int fd, std::uint32_t events, int timeout_ms);

    // 析构时若等待尚未完成, 会主动取消注册, 避免悬挂回调恢复已失效协程
    ~FdAwaiter();

    [[nodiscard]] bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle);
    // 恢复点返回等待结果
    // 返回:
    //   - true: 收到目标事件；
    //   - false: 超时或等待状态无效
    [[nodiscard]] bool await_resume() const noexcept {
      return state_ != nullptr &&
             !state_->timed_out.load(std::memory_order_acquire);
    }

   private:
    friend class AsyncRuntime;

    struct WaitState {
      std::atomic<bool> timed_out{false};
      std::atomic<bool> completed{false};
      std::atomic<bool> cancelled{false};
      std::atomic<std::uint64_t> token{0};
    };

    int fd_;
    std::uint32_t events_;
    int timeout_ms_;
    std::shared_ptr<WaitState> state_;
  };

  // 创建 fd 等待对象
  // 参数语义与 FdAwaiter 构造函数一致
  // 返回: 可 co_await 的 FdAwaiter
  [[nodiscard]] FdAwaiter WaitFor(int fd, std::uint32_t events, int timeout_ms);

  // 按 token 取消等待
  // 参数: token - 等待注册唯一标识
  // 错误语义: token 无效或不存在时静默返回
  void Cancel(std::uint64_t token);

 private:
  struct WaitRegistration {
    std::uint64_t token{0};
    int fd{-1};
    std::uint32_t events{0};
    int timeout_ms{0};
    std::chrono::steady_clock::time_point deadline{};
    std::coroutine_handle<> handle{};
    std::shared_ptr<FdAwaiter::WaitState> state;
  };

  AsyncRuntime();
  ~AsyncRuntime();

  void Run();
  void Enqueue(std::shared_ptr<WaitRegistration> registration);
  void WakeLoop();
  void RegisterPending();
  void ResumeReady(std::shared_ptr<WaitRegistration> registration,
                   bool timed_out);
  void ExpireTimedOut();

  int epoll_fd_{-1};
  int wake_fd_{-1};
  bool running_{true};
  std::thread loop_thread_;
  std::mutex pending_mutex_;
  std::vector<std::shared_ptr<WaitRegistration>> pending_;
  std::mutex registrations_mutex_;
  std::unordered_map<std::uint64_t, std::shared_ptr<WaitRegistration>>
      registrations_;
  std::uint64_t next_token_{1};
};

}  // namespace hxrpc

#endif
