#include "async_runtime.h"
#include "logger.h"
#include <cerrno>
#include <cstring>
#include <sys/eventfd.h>
#include <unistd.h>

namespace hxrpc {

AsyncRuntime &AsyncRuntime::Instance() {
  static AsyncRuntime runtime;
  return runtime;
}

AsyncRuntime::FdAwaiter::FdAwaiter(int fd, std::uint32_t events, int timeout_ms)
    : fd_(fd), events_(events), timeout_ms_(timeout_ms) {}

void AsyncRuntime::FdAwaiter::await_suspend(std::coroutine_handle<> handle) {
  auto registration = std::make_shared<WaitRegistration>();
  registration->fd = fd_;
  registration->events = events_;
  registration->timeout_ms = timeout_ms_;
  registration->deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms_);
  registration->handle = handle;
  registration->awaiter = this;
  AsyncRuntime::Instance().Enqueue(std::move(registration));
}

AsyncRuntime::FdAwaiter AsyncRuntime::WaitFor(int fd, std::uint32_t events,
                                              int timeout_ms) {
  return FdAwaiter(fd, events, timeout_ms);
}

AsyncRuntime::AsyncRuntime() {
  epoll_fd_ = ::epoll_create1(0);
  wake_fd_ = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
  if (epoll_fd_ < 0 || wake_fd_ < 0) {
    LOG(Error) << "failed to create async runtime fds: " << std::strerror(errno);
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
  {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    registration->token = next_token_++;
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

  for (auto &registration : pending_items) {
    epoll_event event{};
    event.events = registration->events | EPOLLONESHOT;
    event.data.u64 = registration->token;
    if (::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, registration->fd, &event) < 0) {
      if (errno != EEXIST ||
          ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, registration->fd, &event) < 0) {
        LOG(Warn) << "async runtime failed to register fd=" << registration->fd
                  << ": " << std::strerror(errno);
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
  registration->awaiter->timed_out_ = timed_out;
  registration->handle.resume();
}

void AsyncRuntime::ExpireTimedOut() {
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

  for (auto &registration : expired) {
    ResumeReady(registration, true);
  }
}

void AsyncRuntime::Run() {
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

} // namespace hxrpc
