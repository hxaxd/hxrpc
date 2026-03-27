#ifndef HXRPC_ASYNC_RUNTIME_H
#define HXRPC_ASYNC_RUNTIME_H

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <sys/epoll.h>
#include <thread>
#include <unordered_map>
#include <vector>

namespace hxrpc {

class AsyncRuntime {
public:
  static AsyncRuntime &Instance();

  AsyncRuntime(const AsyncRuntime &) = delete;
  AsyncRuntime &operator=(const AsyncRuntime &) = delete;

  class FdAwaiter {
  public:
    FdAwaiter(int fd, std::uint32_t events, int timeout_ms);
    ~FdAwaiter();

    [[nodiscard]] bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle);
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

  [[nodiscard]] FdAwaiter WaitFor(int fd, std::uint32_t events, int timeout_ms);
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
  void ResumeReady(std::shared_ptr<WaitRegistration> registration, bool timed_out);
  void ExpireTimedOut();

  int epoll_fd_{-1};
  int wake_fd_{-1};
  bool running_{true};
  std::thread loop_thread_;
  std::mutex pending_mutex_;
  std::vector<std::shared_ptr<WaitRegistration>> pending_;
  std::mutex registrations_mutex_;
  std::unordered_map<std::uint64_t, std::shared_ptr<WaitRegistration>> registrations_;
  std::uint64_t next_token_{1};
};

} // namespace hxrpc

#endif
