#include "reactor.h"

#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "logger.h"

namespace hxrpc {

// Reactor 构造时仅负责创建 epoll 实例与事件缓存
Reactor::Reactor(int max_events)
    : events_(static_cast<std::size_t>(max_events)) {
  epoll_fd_ = ::epoll_create1(0);
}

// 析构时兜底关闭 epoll fd, 确保内核资源释放
Reactor::~Reactor() {
  if (epoll_fd_ >= 0) {
    ::close(epoll_fd_);
  }
}

// 将 fd 与回调一起注册到 epoll；handlers_ 保存回调以便事件分发
void Reactor::Add(int fd, std::uint32_t events,
                  std::function<void(int)> on_readable,
                  std::function<void(int)> on_writable,
                  std::function<void(int)> on_error) {
  epoll_event event{};
  event.events = events;
  event.data.fd = fd;
  ::epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event);
  handlers_[fd] = Handler{events, std::move(on_readable),
                          std::move(on_writable), std::move(on_error)};
}

// 只更新关注事件位图, 不替换原有回调对象
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

// 从 epoll 和本地映射中移除 fd
void Reactor::Remove(int fd) {
  ::epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
  handlers_.erase(fd);
}

// 事件循环设计说明:
// - 采用固定 100ms 轮询超时, 保证 Stop() 不需要额外 eventfd 也能较快生效；
// - EINTR 直接重试, 避免信号中断导致服务退出；
// - 错误事件 (ERR/HUP) 优先于读写事件处理, 避免在异常连接上继续收发
void Reactor::Loop() {
  running_ = true;
  while (running_) {
    const int ready = ::epoll_wait(epoll_fd_, events_.data(),
                                   static_cast<int>(events_.size()), 100);
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
      const auto& event = events_[index];
      auto handler_it = handlers_.find(event.data.fd);
      if (handler_it == handlers_.end()) {
        // fd 可能已被上层移除 (竞态窗口) , 安全跳过即可
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

// 通过运行标志控制循环退出；真正返回点在 Loop 的下一轮 epoll_wait 后
void Reactor::Stop() { running_ = false; }

// 将可读等待委托给 AsyncRuntime, 统一协程调度策略
AsyncRuntime::FdAwaiter Reactor::WaitReadable(int fd, int timeout_ms) {
  return AsyncRuntime::Instance().WaitFor(fd, EPOLLIN, timeout_ms);
}

// 将可写等待委托给 AsyncRuntime, 统一协程调度策略
AsyncRuntime::FdAwaiter Reactor::WaitWritable(int fd, int timeout_ms) {
  return AsyncRuntime::Instance().WaitFor(fd, EPOLLOUT, timeout_ms);
}

}  // namespace hxrpc
