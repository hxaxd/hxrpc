#include <cassert>

#include "service_discovery.h"
#include "settings.h"

int main() {
  // 测试目的: 验证静态服务发现的解析能力, 以及 ZK 路径工具函数的字符串约定
  hxrpc::DiscoveryConfig discovery_config;
  discovery_config.backend = hxrpc::DiscoveryBackend::kStatic;
  discovery_config.static_services["UserServiceRpc.Login"] = {
      hxrpc::Endpoint{"127.0.0.1", 8000},
      hxrpc::Endpoint{"127.0.0.1", 8001},
  };

  hxrpc::StaticResolver resolver(discovery_config);
  const auto instance = resolver.Resolve("UserServiceRpc", "Login");
  assert(instance.has_value());
  assert(instance->endpoint.host == "127.0.0.1");
  // 关键断言: 静态解析结果应落在配置候选集合内
  assert(instance->endpoint.port == 8000 || instance->endpoint.port == 8001);

  // 覆盖场景: ZK 服务路径/方法路径/实例路径拼接规范
  assert(hxrpc::ZkResolver::BuildServicePath("UserServiceRpc") ==
         "/UserServiceRpc");
  assert(hxrpc::ZkResolver::BuildMethodPath("UserServiceRpc", "Login") ==
         "/UserServiceRpc/Login");
  assert(hxrpc::ZkResolver::BuildInstancePath("UserServiceRpc", "Login",
                                              "127.0.0.1:8000") ==
         "/UserServiceRpc/Login/127.0.0.1:8000");

  // 覆盖场景: 实例地址字符串反解析为 Endpoint
  const auto endpoint = hxrpc::ZkResolver::ParseEndpoint("127.0.0.1:8000");
  assert(endpoint.has_value());
  assert(endpoint->port == 8000);

  return 0;
}
