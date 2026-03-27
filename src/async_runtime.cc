// src/async_runtime.cc
// 协程异步运行时实现。
// 核心设计：
// 1) epoll + eventfd 构建单线程事件循环；
// 2) 注册表以 token 跟踪等待项；
// 3) 统一由 ResumeReady 恢复协程并写入超时状态。

#include "async_runtime.h"

#include <sys/eventfd.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "logger.h"

namespace hxrpc {

AsyncRuntime& AsyncRuntime::Instance() {
  static AsyncRuntime runtime;
  return runtime;
}

AsyncRuntime::FdAwaiter::FdAwaiter(int fd, std::uint32_t events, int timeout_ms)
    : fd_(fd),
      events_(events),
      timeout_ms_(timeout_ms),
      state_(std::make_shared<WaitState>()) {}

AsyncRuntime::FdAwaiter::~FdAwaiter() {
  // 设计原因：awaiter 可能先于事件返回被销毁，需要主动取消避免后续恢复落到无效上下文。
  if (!state_) {
    return;
  }
  state_->cancelled.store(true, std::memory_order_relaxed);
  const auto token = state_->token.load(std::memory_order_acquire);
  const bool completed = state_->completed.load(std::memory_order_acquire);
  if (token != 0 && !completed) {
    AsyncRuntime::Instance().Cancel(token);
  }
}

void AsyncRuntime::FdAwaiter::await_suspend(std::coroutine_handle<> handle) {
  // 将协程句柄与等待参数打包登记，真正的 epoll 注册在运行时线程完成。
  auto registration = std::make_shared<WaitRegistration>();
  registration->fd = fd_;
  registration->events = events_;
  registration->timeout_ms = timeout_ms_;
  registration->deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
  registration->handle = handle;
  registration->state = state_;
  AsyncRuntime::Instance().Enqueue(std::move(registration));
}

AsyncRuntime::FdAwaiter AsyncRuntime::WaitFor(int fd, std::uint32_t events,
                                              int timeout_ms) {
  return FdAwaiter(fd, events, timeout_ms);
}

void AsyncRuntime::Cancel(std::uint64_t token) {
  // 错误语义：token 为 0 或未命中时直接返回，保证取消接口幂等。
  if (token == 0) {
    return;
  }

  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    for (auto it = pending_.begin(); it != pending_.end();) {
      if ((*it)->token == token) {
        if ((*it)->state) {
          (*it)->state->completed.store(true, std::memory_order_release);
          (*it)->state->token.store(0, std::memory_order_release);
        }
        it = pending_.erase(it);
      } else {
        ++it;
      }
    }
  }

  std::lock_guard<std::mutex> lock(registrations_mutex_);
  const auto it = registrations_.find(token);
  if (it == registrations_.end()) {
    return;
  }
  (void)::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, it->second->fd, nullptr);
  if (it->second->state) {
    it->second->state->completed.store(true, std::memory_order_release);
    it->second->state->token.store(0, std::memory_order_release);
  }
  registrations_.erase(it);
}

AsyncRuntime::AsyncRuntime() {
  // 初始化运行时基础设施：epoll 用于事件多路复用，eventfd 用于跨线程唤醒。
  epoll_fd_ = ::epoll_create1(0);
  wake_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (epoll_fd_ < 0 || wake_fd_ < 0) {
    LOG(Error) << "failed to create async runtime fds: "
               << std::strerror(errno);
    running_ = false;
    return;
  }

  epoll_event event{};
  event.events = EPOLLIN;
  event.data.u64 = 0;
  if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, wake_fd_, &event) < 0) {
    LOG(Error) << "failed to register async runtime wake fd: "
               << std::strerror(errno);
    running_ = false;
    return;
  }

  loop_thread_ = std::thread([this]() { Run(); });
}

AsyncRuntime::~AsyncRuntime() {
  running_ = false;
  WakeLoop();
  if (loop_thread_.joinable()) {
    loop_thread_.join();
  }
  if (wake_fd_ >= 0) {
    ::close(wake_fd_);
  }
  if (epoll_fd_ >= 0) {
    ::close(epoll_fd_);
  }
}

void AsyncRuntime::Enqueue(std::shared_ptr<WaitRegistration> registration) {
  // 设计原因：注册动作集中在事件线程执行，可避免调用方线程直接触碰 epoll 造成竞争。
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    registration->token = next_token_++;
    if (registration->state) {
      registration->state->token.store(registration->token,
                                       std::memory_order_release);
    }
    pending_.push_back(std::move(registration));
  }
  WakeLoop();
}

void AsyncRuntime::WakeLoop() {
  if (wake_fd_ < 0) {
    return;
  }
  const std::uint64_t value = 1;
  (void)::write(wake_fd_, &value, sizeof(value));
}

void AsyncRuntime::RegisterPending() {
  std::vector<std::shared_ptr<WaitRegistration>> pending_items;
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_items.swap(pending_);
  }

  for (auto& registration : pending_items) {
    epoll_event event{};
    event.events = registration->events | EPOLLONESHOT;
    event.data.u64 = registration->token;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, registration->fd, &event) < 0) {
      if (errno != EEXIST ||
          ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, registration->fd, &event) < 0) {
        LOG(Warn) << "async runtime failed to register fd=" << registration->fd
                  << ": " << std::strerror(errno);
        // 注册失败按超时路径处理：对上层来说都是“未获得可用事件”。
        ResumeReady(registration, true);
        continue;
      }
    }

    std::lock_guard<std::mutex> lock(registrations_mutex_);
    registrations_[registration->token] = std::move(registration);
  }
}

void AsyncRuntime::ResumeReady(std::shared_ptr<WaitRegistration> registration,
                               bool timed_out) {
  // 设计原因：统一在此写入完成态并恢复协程，降低多分支路径下状态不一致风险。
  if (!registration->state) {
    return;
  }
  if (registration->state->cancelled.load(std::memory_order_acquire)) {
    registration->state->completed.store(true, std::memory_order_release);
    registration->state->token.store(0, std::memory_order_release);
    return;
  }
  registration->state->timed_out.store(timed_out, std::memory_order_release);
  registration->state->completed.store(true, std::memory_order_release);
  registration->state->token.store(0, std::memory_order_release);
  registration->handle.resume();
}

void AsyncRuntime::ExpireTimedOut() {
  // 扫描到期等待项并主动摘除 epoll 监听，避免过期句柄长期滞留在注册表中。
  std::vector<std::shared_ptr<WaitRegistration>> expired;
  const auto now = std::chrono::steady_clock::now();

  {
    std::lock_guard<std::mutex> lock(registrations_mutex_);
    for (auto it = registrations_.begin(); it != registrations_.end();) {
      if (it->second->deadline <= now) {
        (void)::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, it->second->fd, nullptr);
        expired.push_back(it->second);
        it = registrations_.erase(it);
      } else {
        ++it;
      }
    }
  }

  for (auto& registration : expired) {
    ResumeReady(registration, true);
  }
}

void AsyncRuntime::Run() {
  // 事件循环主流程：注册新等待 -> 等待就绪事件 -> 恢复协程 -> 处理超时。
  std::vector<epoll_event> events(64);
  while (running_) {
    RegisterPending();
    const int ready = ::epoll_wait(epoll_fd_, events.data(),
                                   static_cast<int>(events.size()), 50);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      LOG(Error) << "async runtime epoll_wait failed: " << std::strerror(errno);
      continue;
    }

    for (int index = 0; index < ready; ++index) {
      const auto token = events[index].data.u64;
      if (token == 0) {
        std::uint64_t value = 0;
        (void)::read(wake_fd_, &value, sizeof(value));
        RegisterPending();
        continue;
      }

      std::shared_ptr<WaitRegistration> registration;
      {
        std::lock_guard<std::mutex> lock(registrations_mutex_);
        const auto it = registrations_.find(token);
        if (it == registrations_.end()) {
          continue;
        }
        registration = it->second;
        registrations_.erase(it);
      }
      (void)::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, registration->fd, nullptr);
      ResumeReady(registration, false);
    }

    ExpireTimedOut();
  }
}

}  // namespace hxrpc
