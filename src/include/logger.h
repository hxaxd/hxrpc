#ifndef HXRPC_LOGGER_H
#define HXRPC_LOGGER_H

#include <expected>
#include <sstream>
#include <string>

namespace hxrpc {

enum class LogLevel { kInfo, kWarn, kError };

struct LoggerOptions {
  bool async_enabled{true};
  bool stderr_enabled{true};
  std::string file_path;
  LogLevel min_level{LogLevel::kInfo};
};

class Logger {
public:
  static Logger &Instance();

  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;

  std::expected<void, std::string> Configure(const LoggerOptions &options);
  void Write(LogLevel level, std::string message);

private:
  Logger();
  ~Logger();
};

class LogMessage {
public:
  explicit LogMessage(LogLevel level) : level_(level) {}

  template <typename T> LogMessage &operator<<(const T &value) {
    stream_ << value;
    return *this;
  }

  ~LogMessage();

private:
  LogLevel level_;
  std::ostringstream stream_;
};

} // namespace hxrpc

#define LOG(level) ::hxrpc::LogMessage(::hxrpc::LogLevel::k##level)

#endif
