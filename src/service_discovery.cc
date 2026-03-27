#include "service_discovery.h"

#include "logger.h"
#include "zookeeperutil.h"

namespace hxrpc {

namespace {

template <typename T>
const T& ChooseItem(const std::vector<T>& items, ResolverSelectionPolicy policy,
                    std::mt19937& random_engine) {
  // 预留多策略扩展点；当前仅实现随机选择
  switch (policy) {
    case ResolverSelectionPolicy::kRandom:
      break;
  }
  std::uniform_int_distribution<std::size_t> distribution(0, items.size() - 1);
  return items[distribution(random_engine)];
}

}  // namespace

StaticResolver::StaticResolver(DiscoveryConfig config)
    : config_(std::move(config)), random_engine_(std::random_device{}()) {}

std::expected<ServiceInstance, RpcError> StaticResolver::Resolve(
    std::string_view service_name, std::string_view method_name) {
  // 静态解析: 键不存在即配置错误, 不做降级猜测
  const auto key = MakeServiceKey(service_name, method_name);
  const auto it = config_.static_services.find(key);
  if (it == config_.static_services.end()) {
    return std::unexpected(RpcError{RpcStatusCode::kDiscoveryError,
                                    "missing static service: " + key});
  }
  return ChooseInstance(it->second);
}

std::string StaticResolver::MakeServiceKey(std::string_view service_name,
                                           std::string_view method_name) {
  return std::string(service_name) + "." + std::string(method_name);
}

std::expected<ServiceInstance, RpcError> StaticResolver::ChooseInstance(
    const std::vector<Endpoint>& endpoints) {
  if (endpoints.empty()) {
    return std::unexpected(RpcError{RpcStatusCode::kDiscoveryError,
                                    "no static instance available"});
  }
  const auto& endpoint =
      ChooseItem(endpoints, config_.selection_policy, random_engine_);
  return ServiceInstance{endpoint.ToString(), endpoint};
}

std::expected<void, RpcError> NoopRegistrar::Register(
    std::string_view service_name, std::string_view method_name,
    const Endpoint& endpoint) {
  (void)service_name;
  (void)method_name;
  (void)endpoint;
  return {};
}

ZkResolver::ZkResolver(std::unique_ptr<ZkClient> client)
    : client_(std::move(client)),
      cache_state_(std::make_shared<CacheState>()),
      random_engine_(std::random_device{}()) {}

std::expected<ServiceInstance, RpcError> ZkResolver::Resolve(
    std::string_view service_name, std::string_view method_name) {
  const std::string method_path = BuildMethodPath(service_name, method_name);
  {
    std::lock_guard<std::mutex> lock(cache_state_->mutex);
    // 先查本地缓存: 命中可降低 ZooKeeper 读取压力与调用延迟
    const auto it = cache_state_->method_instances.find(method_path);
    if (it != cache_state_->method_instances.end()) {
      return ChooseInstance(it->second);
    }
  }

  auto refresh_result = RefreshMethod(service_name, method_name);
  if (!refresh_result) {
    return std::unexpected(refresh_result.error());
  }
  return ChooseInstance(refresh_result.value());
}

std::string ZkResolver::BuildServicePath(std::string_view service_name) {
  return "/" + std::string(service_name);
}

std::string ZkResolver::BuildMethodPath(std::string_view service_name,
                                        std::string_view method_name) {
  return BuildServicePath(service_name) + "/" + std::string(method_name);
}

std::string ZkResolver::BuildInstancePath(std::string_view service_name,
                                          std::string_view method_name,
                                          std::string_view instance_id) {
  return BuildMethodPath(service_name, method_name) + "/" +
         std::string(instance_id);
}

std::expected<Endpoint, RpcError> ZkResolver::ParseEndpoint(
    std::string_view raw) {
  // 节点数据格式约定为 host:port
  const auto separator = raw.find(':');
  if (separator == std::string_view::npos) {
    return std::unexpected(
        RpcError{RpcStatusCode::kDiscoveryError, "endpoint must be host:port"});
  }

  Endpoint endpoint;
  endpoint.host = std::string(raw.substr(0, separator));
  try {
    endpoint.port = static_cast<std::uint16_t>(
        std::stoul(std::string(raw.substr(separator + 1))));
  } catch (...) {
    return std::unexpected(
        RpcError{RpcStatusCode::kDiscoveryError, "endpoint port is invalid"});
  }
  if (!endpoint.IsValid()) {
    return std::unexpected(
        RpcError{RpcStatusCode::kDiscoveryError, "endpoint is incomplete"});
  }
  return endpoint;
}

std::expected<ServiceInstance, RpcError> ZkResolver::ChooseInstance(
    const std::vector<ServiceInstance>& instances) {
  if (instances.empty()) {
    return std::unexpected(RpcError{RpcStatusCode::kDiscoveryError,
                                    "no zookeeper instance available"});
  }
  return ChooseItem(instances, ResolverSelectionPolicy::kRandom,
                    random_engine_);
}

std::expected<std::vector<ServiceInstance>, RpcError> ZkResolver::RefreshMethod(
    std::string_view service_name, std::string_view method_name) {
  const std::string method_path = BuildMethodPath(service_name, method_name);
  // 读取子节点并挂载 watcher: 后续实例变化会回调 OnMethodChanged
  auto children_result = client_->GetChildrenWatched(
      method_path, [this](const std::string& path) { OnMethodChanged(path); });
  if (!children_result) {
    return std::unexpected(children_result.error());
  }

  std::vector<ServiceInstance> instances;
  for (const auto& child : children_result.value()) {
    const std::string instance_path = method_path + "/" + child;
    // 单个实例解析失败不影响整体刷新, 尽量保留可用实例
    auto data_result = client_->GetData(instance_path.c_str());
    if (!data_result) {
      continue;
    }

    auto endpoint_result = ParseEndpoint(data_result.value());
    if (!endpoint_result) {
      continue;
    }
    instances.push_back(ServiceInstance{child, endpoint_result.value()});
  }

  {
    std::lock_guard<std::mutex> lock(cache_state_->mutex);
    cache_state_->method_instances[method_path] = instances;
  }
  return instances;
}

void ZkResolver::OnMethodChanged(std::string method_path) {
  // watcher 回调给出 method 路径, 解析 service/method 后执行定向刷新
  const auto last_separator = method_path.find_last_of('/');
  if (last_separator == std::string::npos || last_separator == 0) {
    return;
  }

  const std::string service_name = method_path.substr(1, last_separator - 1);
  const std::string method_name = method_path.substr(last_separator + 1);
  auto refreshed = RefreshMethod(service_name, method_name);
  if (!refreshed) {
    LOG(Warn) << "failed to refresh watched zookeeper path " << method_path
              << ": " << refreshed.error().message;
  }
}

ZkRegistrar::ZkRegistrar(std::unique_ptr<ZkClient> client)
    : client_(std::move(client)) {}

std::expected<void, RpcError> ZkRegistrar::Register(
    std::string_view service_name, std::string_view method_name,
    const Endpoint& endpoint) {
  // 路径层级: /service/method/instance(host:port)
  const std::string service_path = ZkResolver::BuildServicePath(service_name);
  const std::string method_path =
      ZkResolver::BuildMethodPath(service_name, method_name);
  const std::string instance_id = endpoint.ToString();
  const std::string instance_path =
      ZkResolver::BuildInstancePath(service_name, method_name, instance_id);

  if (auto result = client_->EnsurePath(service_path.c_str()); !result) {
    return std::unexpected(result.error());
  }
  if (auto result = client_->EnsurePath(method_path.c_str()); !result) {
    return std::unexpected(result.error());
  }
  return client_->CreateNode(instance_path.c_str(), instance_id.c_str(),
                             static_cast<int>(instance_id.size()),
                             ZOO_EPHEMERAL);
}

std::unique_ptr<ServiceResolver> DiscoveryFactory::CreateResolver(
    const DiscoveryConfig& config) {
  if (config.backend == DiscoveryBackend::kZookeeper) {
    // ZooKeeper 初始化失败时回退到静态解析, 保证客户端仍可工作
    auto client = std::make_unique<ZkClient>();
    auto start_result = client->Start(config.zookeeper_endpoint);
    if (!start_result) {
      LOG(Warn) << "zookeeper resolver fallback to static: "
                << start_result.error().message;
    } else {
      return std::make_unique<ZkResolver>(std::move(client));
    }
  }
  return std::make_unique<StaticResolver>(config);
}

std::unique_ptr<ServiceRegistrar> DiscoveryFactory::CreateRegistrar(
    const DiscoveryConfig& config) {
  if (config.backend == DiscoveryBackend::kZookeeper) {
    // 注册侧失败时回退 noop, 避免启动阶段因注册不可用而硬失败
    auto client = std::make_unique<ZkClient>();
    auto start_result = client->Start(config.zookeeper_endpoint);
    if (!start_result) {
      LOG(Warn) << "zookeeper registrar fallback to noop: "
                << start_result.error().message;
    } else {
      return std::make_unique<ZkRegistrar>(std::move(client));
    }
  }
  return std::make_unique<NoopRegistrar>();
}

}  // namespace hxrpc
