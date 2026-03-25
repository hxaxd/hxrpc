#ifndef HXRPC_ASYNC_RUNTIME_H
#define HXRPC_ASYNC_RUNTIME_H

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

    [[nodiscard]] bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle);
    [[nodiscard]] bool await_resume() const noexcept { return !timed_out_; }

  private:
    friend class AsyncRuntime;

    int fd_;
    std::uint32_t events_;
    int timeout_ms_;
    bool timed_out_{false};
  };

  [[nodiscard]] FdAwaiter WaitFor(int fd, std::uint32_t events, int timeout_ms);

private:
  struct WaitRegistration {
    std::uint64_t token{0};
    int fd{-1};
    std::uint32_t events{0};
    int timeout_ms{0};
    std::chrono::steady_clock::time_point deadline{};
    std::coroutine_handle<> handle{};
    FdAwaiter *awaiter{nullptr};
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
