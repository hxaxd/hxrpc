// src/logger.cc
// 日志模块实现。
// 核心设计：
// 1) 统一格式化入口，保证 stderr 与文件输出一致；
// 2) 异步模式通过队列 + 后台线程批量刷盘，降低业务线程阻塞；
// 3) 同步模式用于故障定位与极简场景。

#include "logger.h"

#include <chrono>
#include <condition_variable>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace hxrpc {

namespace {

constexpr auto kFlushInterval = std::chrono::milliseconds(100);

struct LogEntry {
  LogLevel level{LogLevel::kInfo};
  std::chrono::system_clock::time_point timestamp{};
  std::string message;
};

class LoggerImpl {
 public:
  // 启动后台日志线程（异步模式与同步模式共用同一实现，便于统一收尾逻辑）。
  LoggerImpl() : worker_([this]() { Run(); }) {}

  ~LoggerImpl() {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
      worker_.join();
    }
    if (file_stream_.is_open()) {
      file_stream_.flush();
    }
  }

  std::expected<void, std::string> Configure(const LoggerOptions& options) {
    // 错误语义：目录创建或文件打开失败时返回 unexpected，不抛异常。
    std::lock_guard<std::mutex> lock(mutex_);

    std::ofstream next_stream;
    if (!options.file_path.empty()) {
      const std::filesystem::path file_path(options.file_path);
      const auto parent = file_path.parent_path();
      if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) {
          return std::unexpected("failed to create log directory: " +
                                 error.message());
        }
      }

      next_stream.open(file_path, std::ios::out | std::ios::app);
      if (!next_stream.is_open()) {
        return std::unexpected("failed to open log file: " + options.file_path);
      }
    }

    options_ = options;
    file_stream_ = std::move(next_stream);
    return {};
  }

  void Write(LogLevel level, std::string message) {
    // 设计原因：统一在入口处做等级过滤，避免后续路径重复判断。
    std::unique_lock<std::mutex> lock(mutex_);
    if (level < options_.min_level) {
      return;
    }

    if (!options_.async_enabled) {
      // 同步模式：调用线程直接输出并 flush，换取最即时的可见性。
      const std::string formatted = FormatLine(LogEntry{
          level, std::chrono::system_clock::now(), std::move(message)});
      WriteUnlocked(formatted);
      if (file_stream_.is_open()) {
        file_stream_.flush();
      }
      if (options_.stderr_enabled) {
        std::cerr.flush();
      }
      return;
    }

    queue_.push_back(
        LogEntry{level, std::chrono::system_clock::now(), std::move(message)});
    lock.unlock();
    cv_.notify_one();
  }

 private:
  static constexpr const char* ToString(LogLevel level) {
    switch (level) {
      case LogLevel::kInfo:
        return "INFO";
      case LogLevel::kWarn:
        return "WARN";
      case LogLevel::kError:
        return "ERROR";
    }
    return "UNKNOWN";
  }

  static std::string FormatLine(LogEntry entry) {
    const std::time_t current_time =
        std::chrono::system_clock::to_time_t(entry.timestamp);

    std::tm time_info{};
#if defined(_WIN32)
    localtime_s(&time_info, &current_time);
#else
    localtime_r(&current_time, &time_info);
#endif

    std::ostringstream stream;
    stream << "[" << ToString(entry.level) << "] "
           << std::put_time(&time_info, "%F %T") << " " << entry.message;
    return stream.str();
  }

  void Run() {
    // 后台线程循环：定时或被唤醒后批量搬运队列，减少锁竞争与频繁 IO。
    std::vector<LogEntry> batch;
    batch.reserve(256);

    std::unique_lock<std::mutex> lock(mutex_);
    while (!stopping_ || !queue_.empty()) {
      cv_.wait_for(lock, kFlushInterval,
                   [this]() { return stopping_ || !queue_.empty(); });

      if (queue_.empty()) {
        continue;
      }

      while (!queue_.empty()) {
        batch.push_back(std::move(queue_.front()));
        queue_.pop_front();
      }

      lock.unlock();
      WriteBatch(batch);
      batch.clear();
      lock.lock();
    }
  }

  void WriteUnlocked(const std::string& line) {
    if (options_.stderr_enabled) {
      std::cerr << line << std::endl;
    }
    if (file_stream_.is_open()) {
      file_stream_ << line << std::endl;
    }
  }

  void WriteBatch(const std::vector<LogEntry>& batch) {
    // 设计原因：批量写可摊薄系统调用与 flush 成本，提升高并发日志吞吐。
    for (const auto& entry : batch) {
      WriteUnlocked(
          FormatLine(LogEntry{entry.level, entry.timestamp, entry.message}));
    }
    if (file_stream_.is_open()) {
      file_stream_.flush();
    }
    if (options_.stderr_enabled) {
      std::cerr.flush();
    }
  }

  LoggerOptions options_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<LogEntry> queue_;
  std::ofstream file_stream_;
  bool stopping_{false};
  std::thread worker_;
};

LoggerImpl& GetImpl() {
  static LoggerImpl impl;
  return impl;
}

}  // namespace

Logger& Logger::Instance() {
  static Logger logger;
  return logger;
}

Logger::Logger() = default;
Logger::~Logger() = default;

std::expected<void, std::string> Logger::Configure(
    const LoggerOptions& options) {
  return GetImpl().Configure(options);
}

void Logger::Write(LogLevel level, std::string message) {
  GetImpl().Write(level, std::move(message));
}

LogMessage::~LogMessage() { Logger::Instance().Write(level_, stream_.str()); }

}  // namespace hxrpc
