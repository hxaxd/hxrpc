#ifndef HXRPC_LOGGER_H
#define HXRPC_LOGGER_H

// src/include/logger.h
// 统一日志接口
// 提供: 日志级别控制, 同步/异步输出配置, 流式拼接写法与全局单例访问

#include <expected>
#include <sstream>
#include <string>

namespace hxrpc {

enum class LogLevel { kInfo, kWarn, kError };

struct LoggerOptions {
  // 是否启用异步日志队列
  bool async_enabled{true};
  // 是否输出到标准错误流
  bool stderr_enabled{true};
  // 日志文件路径；为空表示不写文件
  std::string file_path;
  // 低于该等级的日志会被过滤
  LogLevel min_level{LogLevel::kInfo};
};

class Logger {
 public:
  // 获取全局 Logger 单例
  // 返回: Logger 引用
  static Logger& Instance();

  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;

  // 更新日志配置
  // 参数: options - 输出目标与过滤策略
  // 返回: 成功返回空 expected；失败返回错误文本
  // 错误语义: 不抛异常, 调用方可选择继续运行或降级
  std::expected<void, std::string> Configure(const LoggerOptions& options);

  // 写入一条日志
  // 参数: level - 日志等级；message - 原始文本
  // 返回: void
  void Write(LogLevel level, std::string message);

 private:
  Logger();
  ~Logger();
};

class LogMessage {
 public:
  // 构造流式日志消息对象
  // 参数: level - 目标日志等级
  explicit LogMessage(LogLevel level) : level_(level) {}

  template <typename T>
  LogMessage& operator<<(const T& value) {
    stream_ << value;
    return *this;
  }

  // 析构时自动提交日志, 保证 `LOG(...) << ...` 语法在作用域结束时落盘/输出
  ~LogMessage();

 private:
  LogLevel level_;
  std::ostringstream stream_;
};

}  // namespace hxrpc

#define LOG(level) ::hxrpc::LogMessage(::hxrpc::LogLevel::k##level)

#endif
