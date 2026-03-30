#ifndef HXRPC_SETTINGS_H
#define HXRPC_SETTINGS_H

// src/include/settings.h
// 运行时配置模型与装载入口
// 职责: 把通用配置键值映射为强类型配置结构 (服务端/客户端) ,
// 并集中定义发现, 序列化, 调用超时等核心选项

#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

#include "config.h"
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

class SettingsLoader {
 public:
  // 解析服务端配置
  // 参数: config - 已加载的原始配置表
  // 返回: ServerConfig若关键字段非法则返回 unexpected(error)
  [[nodiscard]] static std::expected<ServerConfig, std::string>
  LoadServerConfig(const hxrpcconfig& config);

  // 解析客户端配置
  // 参数: config - 已加载的原始配置表
  // 返回: ClientConfig超时字段非法时返回 unexpected(error)
  [[nodiscard]] static std::expected<ClientConfig, std::string>
  LoadClientConfig(const hxrpcconfig& config);

 private:
  [[nodiscard]] static std::expected<Endpoint, std::string> ParseEndpoint(
      std::string_view raw, std::string_view field_name);
  [[nodiscard]] static DiscoveryBackend ParseDiscoveryBackend(
      std::string_view raw);
  [[nodiscard]] static std::expected<std::vector<Endpoint>, std::string>
  ParseEndpointList(std::string_view raw, std::string_view field_name);
  [[nodiscard]] static DiscoveryConfig LoadDiscoveryConfig(
      const hxrpcconfig& config);
};

}  // namespace hxrpc

#endif
