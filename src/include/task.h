#ifndef HXRPC_TASK_H
#define HXRPC_TASK_H

#include <condition_variable>
#include <coroutine>
#include <exception>
#include <expected>
#include <mutex>
#include <optional>
#include <utility>

namespace hxrpc {

// Task<T> 是一个可 co_await + 可同步 Get()的轻量协程结果容器:
// - 协程结束后把值/异常写入 promise；
// - 调用方既可在协程链路里 co_await, 也可在同步场景阻塞等待结果
template <typename T>
class Task {
 public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  explicit Task(handle_type handle) : handle_(handle) {}
  Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  Task& operator=(Task&& other) noexcept {
    if (this == &other) {
      return *this;
    }
    if (handle_ != nullptr) {
      handle_.destroy();
    }
    handle_ = std::exchange(other.handle_, {});
    return *this;
  }

  ~Task() {
    if (handle_ != nullptr) {
      handle_.destroy();
    }
  }

  // 同步等待协程完成并取回结果；若协程抛异常则在此重新抛出
  T Get() {
    auto& promise = handle_.promise();
    std::unique_lock<std::mutex> lock(promise.mutex);
    promise.cv.wait(lock, [&promise] { return promise.done; });
    if (promise.exception != nullptr) {
      std::rethrow_exception(promise.exception);
    }
    return std::move(*promise.value);
  }

  struct Awaiter {
    handle_type handle;
    // owns_handle=true 表示该 Awaiter 接管了协程句柄的销毁责任 (见右值
    // co_await)
    bool owns_handle{false};

    ~Awaiter() {
      if (owns_handle && handle != nullptr) {
        handle.destroy();
      }
    }

    [[nodiscard]] bool await_ready() const noexcept {
      return handle.promise().done;
    }

    // 挂起当前协程, 并把 continuation 写入被等待 Task 的 promise
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> continuation) noexcept {
      handle.promise().continuation = continuation;
      return handle;
    }

    T await_resume() {
      auto& promise = handle.promise();
      if (promise.exception != nullptr) {
        std::rethrow_exception(promise.exception);
      }
      return std::move(*promise.value);
    }
  };

  [[nodiscard]] Awaiter operator co_await() & noexcept {
    // 左值 co_await: 不转移 Task 所有权, 句柄仍由 Task 对象析构时释放
    return Awaiter{handle_, false};
  }
  [[nodiscard]] Awaiter operator co_await() && noexcept {
    // 右值 co_await: 把句柄所有权转移给 Awaiter, 避免悬空引用
    return Awaiter{std::exchange(handle_, {}), true};
  }

  struct promise_type {
    std::mutex mutex;
    std::condition_variable cv;
    std::optional<T> value;
    std::exception_ptr exception;
    bool done{false};
    std::coroutine_handle<> continuation{};

    Task get_return_object() { return Task{handle_type::from_promise(*this)}; }
    std::suspend_never initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
      [[nodiscard]] bool await_ready() const noexcept { return false; }

      template <typename Promise>
      std::coroutine_handle<> await_suspend(
          std::coroutine_handle<Promise> handle) noexcept {
        auto& promise = handle.promise();
        {
          // 先在锁内标记 done, 再通知等待线程, 避免竞争条件
          std::lock_guard<std::mutex> lock(promise.mutex);
          promise.done = true;
        }
        promise.cv.notify_all();
        if (promise.continuation != nullptr) {
          return promise.continuation;
        }
        return std::noop_coroutine();
      }

      void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return {}; }

    template <typename Value>
    void return_value(Value&& value_in) {
      // 结果保存在 optional 中, 供 Get()/await_resume() 统一读取
      value = std::forward<Value>(value_in);
    }

    void unhandled_exception() { exception = std::current_exception(); }
  };

 private:
  handle_type handle_{};
};

template <>
class Task<void> {
 public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  explicit Task(handle_type handle) : handle_(handle) {}
  Task(Task&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
  Task(const Task&) = delete;
  Task& operator=(const Task&) = delete;
  ~Task() {
    if (handle_ != nullptr) {
      handle_.destroy();
    }
  }

  // void 版本仅等待完成并传播异常, 不返回值
  void Get() {
    auto& promise = handle_.promise();
    std::unique_lock<std::mutex> lock(promise.mutex);
    promise.cv.wait(lock, [&promise] { return promise.done; });
    if (promise.exception != nullptr) {
      std::rethrow_exception(promise.exception);
    }
  }

  struct Awaiter {
    handle_type handle;
    bool owns_handle{false};

    ~Awaiter() {
      if (owns_handle && handle != nullptr) {
        handle.destroy();
      }
    }

    [[nodiscard]] bool await_ready() const noexcept {
      return handle.promise().done;
    }

    // 与 Task<T> 同理: 记录 continuation 并切换执行到被等待协程
    std::coroutine_handle<> await_suspend(
        std::coroutine_handle<> continuation) noexcept {
      handle.promise().continuation = continuation;
      return handle;
    }

    void await_resume() {
      auto& promise = handle.promise();
      if (promise.exception != nullptr) {
        std::rethrow_exception(promise.exception);
      }
    }
  };

  [[nodiscard]] Awaiter operator co_await() & noexcept {
    // 左值 co_await: 句柄生命周期由 Task<void> 自身管理
    return Awaiter{handle_, false};
  }
  [[nodiscard]] Awaiter operator co_await() && noexcept {
    // 右值 co_await: 句柄所有权转移给 Awaiter
    return Awaiter{std::exchange(handle_, {}), true};
  }

  struct promise_type {
    std::mutex mutex;
    std::condition_variable cv;
    std::exception_ptr exception;
    bool done{false};
    std::coroutine_handle<> continuation{};

    Task get_return_object() { return Task{handle_type::from_promise(*this)}; }
    std::suspend_never initial_suspend() noexcept { return {}; }

    struct FinalAwaiter {
      [[nodiscard]] bool await_ready() const noexcept { return false; }

      template <typename Promise>
      std::coroutine_handle<> await_suspend(
          std::coroutine_handle<Promise> handle) noexcept {
        auto& promise = handle.promise();
        {
          // 与泛型版本一致: 在通知前设置完成标记, 保证可见性顺序
          std::lock_guard<std::mutex> lock(promise.mutex);
          promise.done = true;
        }
        promise.cv.notify_all();
        if (promise.continuation != nullptr) {
          return promise.continuation;
        }
        return std::noop_coroutine();
      }

      void await_resume() noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return {}; }
    void return_void() noexcept {}
    void unhandled_exception() { exception = std::current_exception(); }
  };

 private:
  handle_type handle_{};
};

}  // namespace hxrpc

#endif
