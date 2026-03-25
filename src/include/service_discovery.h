#ifndef HXRPC_SERVICE_DISCOVERY_H
#define HXRPC_SERVICE_DISCOVERY_H

#include "settings.h"
#include "types.h"
#include <expected>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class ZkClient;

namespace hxrpc {

class ServiceResolver {
public:
  virtual ~ServiceResolver() = default;
  [[nodiscard]] virtual std::expected<ServiceInstance, RpcError>
  Resolve(std::string_view service_name, std::string_view method_name) = 0;
};

class ServiceRegistrar {
public:
  virtual ~ServiceRegistrar() = default;
  [[nodiscard]] virtual std::expected<void, RpcError>
  Register(std::string_view service_name, std::string_view method_name,
           const Endpoint &endpoint) = 0;
};

class StaticResolver final : public ServiceResolver {
public:
  explicit StaticResolver(DiscoveryConfig config);

  [[nodiscard]] std::expected<ServiceInstance, RpcError>
  Resolve(std::string_view service_name, std::string_view method_name) override;

  [[nodiscard]] static std::string MakeServiceKey(std::string_view service_name,
                                                  std::string_view method_name);

private:
  [[nodiscard]] std::expected<ServiceInstance, RpcError>
  ChooseInstance(const std::vector<Endpoint> &endpoints);

  DiscoveryConfig config_;
  std::mt19937 random_engine_;
};

class NoopRegistrar final : public ServiceRegistrar {
public:
  [[nodiscard]] std::expected<void, RpcError>
  Register(std::string_view service_name, std::string_view method_name,
           const Endpoint &endpoint) override;
};

class ZkResolver final : public ServiceResolver {
public:
  explicit ZkResolver(std::unique_ptr<ZkClient> client);

  [[nodiscard]] std::expected<ServiceInstance, RpcError>
  Resolve(std::string_view service_name, std::string_view method_name) override;

  [[nodiscard]] static std::string BuildServicePath(std::string_view service_name);
  [[nodiscard]] static std::string BuildMethodPath(std::string_view service_name,
                                                   std::string_view method_name);
  [[nodiscard]] static std::string BuildInstancePath(std::string_view service_name,
                                                     std::string_view method_name,
                                                     std::string_view instance_id);
  [[nodiscard]] static std::expected<Endpoint, RpcError>
  ParseEndpoint(std::string_view raw);

private:
  struct CacheState {
    std::mutex mutex;
    std::unordered_map<std::string, std::vector<ServiceInstance>> method_instances;
  };

  std::expected<std::vector<ServiceInstance>, RpcError>
  RefreshMethod(std::string_view service_name, std::string_view method_name);
  void OnMethodChanged(std::string method_path);
  [[nodiscard]] std::expected<ServiceInstance, RpcError>
  ChooseInstance(const std::vector<ServiceInstance> &instances);

  std::unique_ptr<ZkClient> client_;
  std::shared_ptr<CacheState> cache_state_;
  std::mt19937 random_engine_;
};

class ZkRegistrar final : public ServiceRegistrar {
public:
  explicit ZkRegistrar(std::unique_ptr<ZkClient> client);

  [[nodiscard]] std::expected<void, RpcError>
  Register(std::string_view service_name, std::string_view method_name,
           const Endpoint &endpoint) override;

private:
  std::unique_ptr<ZkClient> client_;
};

class DiscoveryFactory {
public:
  [[nodiscard]] static std::unique_ptr<ServiceResolver>
  CreateResolver(const DiscoveryConfig &config);
  [[nodiscard]] static std::unique_ptr<ServiceRegistrar>
  CreateRegistrar(const DiscoveryConfig &config);
};

} // namespace hxrpc

#endif
