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

  std::expected<void, std::string> Configure(const LoggerOptions &options) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ofstream next_stream;
    if (!options.file_path.empty()) {
      const std::filesystem::path file_path(options.file_path);
      const auto parent = file_path.parent_path();
      if (!parent.empty()) {
        std::error_code error;
        std::filesystem::create_directories(parent, error);
        if (error) {
          return std::unexpected("failed to create log directory: " + error.message());
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
    std::unique_lock<std::mutex> lock(mutex_);
    if (level < options_.min_level) {
      return;
    }

    if (!options_.async_enabled) {
      const std::string formatted = FormatLine(
          LogEntry{level, std::chrono::system_clock::now(), std::move(message)});
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
  static constexpr const char *ToString(LogLevel level) {
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

  void WriteUnlocked(const std::string &line) {
    if (options_.stderr_enabled) {
      std::cerr << line << std::endl;
    }
    if (file_stream_.is_open()) {
      file_stream_ << line << std::endl;
    }
  }

  void WriteBatch(const std::vector<LogEntry> &batch) {
    for (const auto &entry : batch) {
      WriteUnlocked(FormatLine(LogEntry{entry.level, entry.timestamp, entry.message}));
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

LoggerImpl &GetImpl() {
  static LoggerImpl impl;
  return impl;
}

} // namespace

Logger &Logger::Instance() {
  static Logger logger;
  return logger;
}

Logger::Logger() = default;
Logger::~Logger() = default;

std::expected<void, std::string> Logger::Configure(const LoggerOptions &options) {
  return GetImpl().Configure(options);
}

void Logger::Write(LogLevel level, std::string message) {
  GetImpl().Write(level, std::move(message));
}

LogMessage::~LogMessage() { Logger::Instance().Write(level_, stream_.str()); }

} // namespace hxrpc
