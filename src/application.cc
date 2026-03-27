// src/application.cc
// 应用初始化实现
// 关键流程: 解析命令行配置路径 -> 加载配置文件 -> 初始化日志系统
// 设计原因: 将全局初始化前置到单点, 降低业务示例与框架组件的耦合

#include "application.h"

#include <cctype>
#include <cstdlib>

#include "logger.h"

hxrpcApplication* hxrpcApplication::application_ = nullptr;
hxrpcconfig hxrpcApplication::config_{};
std::mutex hxrpcApplication::mutex_{};

namespace {

bool ParseBoolWithDefault(std::string value, bool default_value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  if (value.empty()) {
    return default_value;
  }
  if (value == "1" || value == "true" || value == "yes" || value == "on") {
    return true;
  }
  if (value == "0" || value == "false" || value == "no" || value == "off") {
    return false;
  }
  return default_value;
}

void ConfigureLogger(const hxrpcconfig& config) {
  // 设计原因: 日志系统支持运行时配置, 优先读取配置值, 解析失败时回退默认行为
  hxrpc::LoggerOptions options;
  options.async_enabled =
      ParseBoolWithDefault(config.Load("logging.async"), true);
  options.stderr_enabled =
      ParseBoolWithDefault(config.Load("logging.to_stderr"), true);
  options.file_path = config.Load("logging.path");

  if (auto result = hxrpc::Logger::Instance().Configure(options); !result) {
    LOG(Warn) << "failed to configure logger: " << result.error();
  }
}

}  // namespace

void hxrpcApplication::Init(int argc, char** argv) {
  // 参数: argc/argv - 启动参数
  // 错误语义: 未提供 -i 或配置文件加载失败时, 直接 exit(EXIT_FAILURE)
  std::string config_file;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "-i" && index + 1 < argc) {
      config_file = argv[++index];
    }
  }

  if (config_file.empty()) {
    LOG(Error) << "missing config file, use: command -i <config-file>";
    std::exit(EXIT_FAILURE);
  }

  if (auto result = config_.LoadConfigFile(config_file.c_str()); !result) {
    LOG(Error) << "failed to load config file '" << config_file
               << "': " << result.error();
    std::exit(EXIT_FAILURE);
  }

  ConfigureLogger(config_);
}

hxrpcApplication& hxrpcApplication::GetInstance() {
  // 设计原因: 采用懒加载 + atexit 销毁, 避免静态初始化顺序问题
  std::lock_guard<std::mutex> lock(mutex_);
  if (application_ == nullptr) {
    application_ = new hxrpcApplication();
    std::atexit(DeleteInstance);
  }
  return *application_;
}

hxrpcconfig& hxrpcApplication::GetConfig() { return config_; }

void hxrpcApplication::DeleteInstance() {
  // 仅在进程退出阶段调用, 释放单例实例
  delete application_;
  application_ = nullptr;
}
