#ifndef HXRPC_SERVICE_DISCOVERY_H
#define HXRPC_SERVICE_DISCOVERY_H

#include <expected>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "settings.h"
#include "types.h"

class ZkClient;

namespace hxrpc {

// 服务解析抽象: 根据 service/method 选择一个可调用实例
class ServiceResolver {
 public:
  virtual ~ServiceResolver() = default;
  // 返回: 成功为实例信息失败为发现错误 (配置缺失, 后端不可用, 无实例等)
  [[nodiscard]] virtual std::expected<ServiceInstance, RpcError> Resolve(
      std::string_view service_name, std::string_view method_name) = 0;
};

// 服务注册抽象: 在发现后端注册当前服务实例
class ServiceRegistrar {
 public:
  virtual ~ServiceRegistrar() = default;
  // endpoint 通常编码为 host:port失败返回发现层错误
  [[nodiscard]] virtual std::expected<void, RpcError> Register(
      std::string_view service_name, std::string_view method_name,
      const Endpoint& endpoint) = 0;
};

// 静态解析器: 从配置表直接查找候选实例, 不依赖外部发现系统
class StaticResolver final : public ServiceResolver {
 public:
  explicit StaticResolver(DiscoveryConfig config);

  [[nodiscard]] std::expected<ServiceInstance, RpcError> Resolve(
      std::string_view service_name, std::string_view method_name) override;

  [[nodiscard]] static std::string MakeServiceKey(std::string_view service_name,
                                                  std::string_view method_name);

 private:
  // 在候选 endpoint 中按策略选择一个实例空列表返回发现错误
  [[nodiscard]] std::expected<ServiceInstance, RpcError> ChooseInstance(
      const std::vector<Endpoint>& endpoints);

  DiscoveryConfig config_;
  std::mt19937 random_engine_;
};

class NoopRegistrar final : public ServiceRegistrar {
 public:
  // 无操作注册器: 用于不需要注册能力的场景 (例如纯静态配置)
  [[nodiscard]] std::expected<void, RpcError> Register(
      std::string_view service_name, std::string_view method_name,
      const Endpoint& endpoint) override;
};

// ZooKeeper 解析器: 基于层级路径读取实例并维护本地缓存
class ZkResolver final : public ServiceResolver {
 public:
  explicit ZkResolver(std::unique_ptr<ZkClient> client);

  [[nodiscard]] std::expected<ServiceInstance, RpcError> Resolve(
      std::string_view service_name, std::string_view method_name) override;

  [[nodiscard]] static std::string BuildServicePath(
      std::string_view service_name);
  [[nodiscard]] static std::string BuildMethodPath(
      std::string_view service_name, std::string_view method_name);
  [[nodiscard]] static std::string BuildInstancePath(
      std::string_view service_name, std::string_view method_name,
      std::string_view instance_id);
  [[nodiscard]] static std::expected<Endpoint, RpcError> ParseEndpoint(
      std::string_view raw);

 private:
  // 缓存按 method_path 组织, watch 回调触发时会刷新对应条目
  struct CacheState {
    std::mutex mutex;
    std::unordered_map<std::string, std::vector<ServiceInstance>>
        method_instances;
  };

  std::expected<std::vector<ServiceInstance>, RpcError> RefreshMethod(
      std::string_view service_name, std::string_view method_name);
  void OnMethodChanged(std::string method_path);
  [[nodiscard]] std::expected<ServiceInstance, RpcError> ChooseInstance(
      const std::vector<ServiceInstance>& instances);

  std::unique_ptr<ZkClient> client_;
  std::shared_ptr<CacheState> cache_state_;
  std::mt19937 random_engine_;
};

// ZooKeeper 注册器: 将实例以临时节点写入, 进程退出后节点自动失效
class ZkRegistrar final : public ServiceRegistrar {
 public:
  explicit ZkRegistrar(std::unique_ptr<ZkClient> client);

  [[nodiscard]] std::expected<void, RpcError> Register(
      std::string_view service_name, std::string_view method_name,
      const Endpoint& endpoint) override;

 private:
  std::unique_ptr<ZkClient> client_;
};

class DiscoveryFactory {
 public:
  // 工厂会按配置选择后端若 ZooKeeper 不可用会回退到可用实现
  [[nodiscard]] static std::unique_ptr<ServiceResolver> CreateResolver(
      const DiscoveryConfig& config);
  [[nodiscard]] static std::unique_ptr<ServiceRegistrar> CreateRegistrar(
      const DiscoveryConfig& config);
};

}  // namespace hxrpc

#endif
