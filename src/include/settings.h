#ifndef HXRPC_SETTINGS_H
#define HXRPC_SETTINGS_H

// src/include/settings.h
// 运行时配置模型与装载入口
// 职责: 从配置文件路径加载强类型配置结构, 并集中定义发现,
// 序列化, 调用超时, 日志等核心选项

#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "logger.h"
#include "types.h"

namespace hxrpc {

enum class DiscoveryBackend {
  // 静态地址发现: 由配置文件直接提供服务实例列表
  kStatic,
  // ZooKeeper 发现: 通过注册中心拉取可用实例
  kZookeeper,
};

enum class SerializationBackend {
  // Protobuf 序列化
  kProtobuf,
};

enum class LoggerMode {
  // 当前线程直接写日志
  kSync,
  // 通过后台线程异步刷写
  kAsync,
};

enum class LoggerSink {
  // 仅写标准错误
  kStderr,
  // 仅写文件
  kFile,
  // 同时写标准错误与文件
  kStderrAndFile,
};

struct ReactorConfig {
  // 单次事件轮询最多处理的事件数
  int max_events{128};
};

struct DiscoveryConfig {
  // 服务发现后端类型
  DiscoveryBackend backend{DiscoveryBackend::kStatic};
  // ZooKeeper 地址 (仅 backend=kZookeeper 时生效)
  Endpoint zookeeper_endpoint;
  // 多实例选择策略
  ResolverSelectionPolicy selection_policy{ResolverSelectionPolicy::kRandom};
  // 静态服务映射: service_key -> endpoint 列表
  std::unordered_map<std::string, std::vector<Endpoint>> static_services;
};

struct SerializationConfig {
  // 序列化后端
  SerializationBackend backend{SerializationBackend::kProtobuf};
};

struct LoggerConfig {
  // 日志写入模式
  LoggerMode mode{LoggerMode::kAsync};
  // 日志输出目标
  LoggerSink sink{LoggerSink::kStderr};
  // 文件输出路径 (仅 sink 包含 file 时生效)
  std::string file_path;
  // 最低输出等级
  LogLevel min_level{LogLevel::kInfo};

  [[nodiscard]] LoggerOptions ToOptions() const {
    LoggerOptions options;
    options.async_enabled = mode == LoggerMode::kAsync;
    options.stderr_enabled =
        sink == LoggerSink::kStderr || sink == LoggerSink::kStderrAndFile;
    options.file_path = file_path;
    options.min_level = min_level;
    return options;
  }
};

struct ServerConfig {
  // RPC 服务监听地址
  Endpoint listen_endpoint;
  ReactorConfig reactor;
  DiscoveryConfig discovery;
  SerializationConfig serialization;
};

struct ClientConfig {
  // 客户端的发现与序列化策略
  DiscoveryConfig discovery;
  SerializationConfig serialization;
  // 调用选项 (超时, metadata 等)
  CallOptions call_options;
};

struct ServerSettings {
  ServerConfig config;

  [[nodiscard]] static std::expected<ServerSettings, std::string> Load(
      std::string_view path);
};

struct ClientSettings {
  ClientConfig config;

  [[nodiscard]] static std::expected<ClientSettings, std::string> Load(
      std::string_view path);
};

struct LoggerSettings {
  LoggerConfig config;

  [[nodiscard]] static std::expected<LoggerSettings, std::string> Load(
      std::string_view path);
};

}  // namespace hxrpc

#endif
