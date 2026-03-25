#ifndef HXRPC_TASK_H
#define HXRPC_TASK_H

#include <condition_variable>
#include <coroutine>
#include <expected>
#include <exception>
#include <mutex>
#include <optional>
#include <utility>

namespace hxrpc {

template <typename T> class Task {
public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  explicit Task(handle_type handle) : handle_(handle) {}
  Task(Task &&other) noexcept : handle_(std::exchange(other.handle_, {})) {}
  Task(const Task &) = delete;
  Task &operator=(const Task &) = delete;
  Task &operator=(Task &&other) noexcept {
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

  T Get() {
    auto &promise = handle_.promise();
    std::unique_lock<std::mutex> lock(promise.mutex);
    promise.cv.wait(lock, [&promise] { return promise.done; });
    if (promise.exception != nullptr) {
      std::rethrow_exception(promise.exception);
    }
    return std::move(*promise.value);
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

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
      handle.promise().continuation = continuation;
      return handle;
    }

    T await_resume() {
      auto &promise = handle.promise();
      if (promise.exception != nullptr) {
        std::rethrow_exception(promise.exception);
      }
      return std::move(*promise.value);
    }
  };

  [[nodiscard]] Awaiter operator co_await() & noexcept { return Awaiter{handle_, false}; }
  [[nodiscard]] Awaiter operator co_await() && noexcept {
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
      std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        auto &promise = handle.promise();
        {
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
    void return_value(Value &&value_in) {
      value = std::forward<Value>(value_in);
    }

    void unhandled_exception() { exception = std::current_exception(); }
  };

private:
  handle_type handle_{};
};

template <> class Task<void> {
public:
  struct promise_type;
  using handle_type = std::coroutine_handle<promise_type>;

  explicit Task(handle_type handle) : handle_(handle) {}
  Task(Task &&other) noexcept : handle_(std::exchange(other.handle_, {})) {}
  Task(const Task &) = delete;
  Task &operator=(const Task &) = delete;
  ~Task() {
    if (handle_ != nullptr) {
      handle_.destroy();
    }
  }

  void Get() {
    auto &promise = handle_.promise();
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

    std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept {
      handle.promise().continuation = continuation;
      return handle;
    }

    void await_resume() {
      auto &promise = handle.promise();
      if (promise.exception != nullptr) {
        std::rethrow_exception(promise.exception);
      }
    }
  };

  [[nodiscard]] Awaiter operator co_await() & noexcept { return Awaiter{handle_, false}; }
  [[nodiscard]] Awaiter operator co_await() && noexcept {
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
      std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> handle) noexcept {
        auto &promise = handle.promise();
        {
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

} // namespace hxrpc

#endif
