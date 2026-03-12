#ifndef hxrpc_LOG_H
#define hxrpc_LOG_H
#include <glog/logging.h>
#include <string>
// 采用RAII的思想
class hxrpcLogger {
public:
  // 构造函数，自动初始化glog
  explicit hxrpcLogger(const char *argv0) {
    google::InitGoogleLogging(argv0);
    FLAGS_colorlogtostderr = true; // 启用彩色日志
    FLAGS_logtostderr = true;      // 默认输出标准错误
  }
  ~hxrpcLogger() { google::ShutdownGoogleLogging(); }
  // 提供静态日志方法
  static void Info(const std::string &message) { LOG(INFO) << message; }
  static void Warning(const std::string &message) { LOG(WARNING) << message; }
  static void ERROR(const std::string &message) { LOG(ERROR) << message; }
  static void Fatal(const std::string &message) { LOG(FATAL) << message; }
  // 禁用拷贝构造函数和重载赋值函数
private:
  hxrpcLogger(const hxrpcLogger &) = delete;
  hxrpcLogger &operator=(const hxrpcLogger &) = delete;
};

#endif