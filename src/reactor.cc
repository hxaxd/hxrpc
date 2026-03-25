#include "reactor.h"
#include "logger.h"
#include <cerrno>
#include <cstring>
#include <unistd.h>

namespace hxrpc {

Reactor::Reactor(int max_events) : events_(static_cast<std::size_t>(max_events)) {
  epoll_fd_ = ::epoll_create1(0);
}

Reactor::~Reactor() {
  if (epoll_fd_ >= 0) {
    ::close(epoll_fd_);
  }
}

void Reactor::Add(int fd, std::uint32_t events,
                  std::function<void(int)> on_readable,
                  std::function<void(int)> on_writable,
                  std::function<void(int)> on_error) {
  epoll_event event{};
  event.events = events;
  event.data.fd = fd;
  ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event);
  handlers_[fd] = Handler{events, std::move(on_readable), std::move(on_writable),
                          std::move(on_error)};
}

void Reactor::Update(int fd, std::uint32_t events) {
  auto it = handlers_.find(fd);
  if (it == handlers_.end()) {
    return;
  }
  epoll_event event{};
  event.events = events;
  event.data.fd = fd;
  ::epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event);
  it->second.events = events;
}

void Reactor::Remove(int fd) {
  ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
  handlers_.erase(fd);
}

void Reactor::Loop() {
  running_ = true;
  while (running_) {
    const int ready =
        ::epoll_wait(epoll_fd_, events_.data(), static_cast<int>(events_.size()), 100);
    if (ready < 0) {
      if (errno == EINTR) {
        continue;
      }
      LOG(Error) << "epoll_wait failed: " << std::strerror(errno);
      break;
    }
    if (ready == 0) {
      continue;
    }

    for (int index = 0; index < ready; ++index) {
      const auto &event = events_[index];
      auto handler_it = handlers_.find(event.data.fd);
      if (handler_it == handlers_.end()) {
        continue;
      }

      if ((event.events & (EPOLLERR | EPOLLHUP)) != 0) {
        if (handler_it->second.on_error) {
          handler_it->second.on_error(event.data.fd);
        }
        continue;
      }
      if ((event.events & EPOLLIN) != 0 && handler_it->second.on_readable) {
        handler_it->second.on_readable(event.data.fd);
      }
      if ((event.events & EPOLLOUT) != 0 && handler_it->second.on_writable) {
        handler_it->second.on_writable(event.data.fd);
      }
    }
  }
}

void Reactor::Stop() { running_ = false; }

AsyncRuntime::FdAwaiter Reactor::WaitReadable(int fd, int timeout_ms) {
  return AsyncRuntime::Instance().WaitFor(fd, EPOLLIN, timeout_ms);
}

AsyncRuntime::FdAwaiter Reactor::WaitWritable(int fd, int timeout_ms) {
  return AsyncRuntime::Instance().WaitFor(fd, EPOLLOUT, timeout_ms);
}

} // namespace hxrpc
