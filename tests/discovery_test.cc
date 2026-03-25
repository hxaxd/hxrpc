#include "service_discovery.h"
#include "settings.h"
#include <cassert>

int main() {
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
  assert(instance->endpoint.port == 8000 || instance->endpoint.port == 8001);

  assert(hxrpc::ZkResolver::BuildServicePath("UserServiceRpc") ==
         "/UserServiceRpc");
  assert(hxrpc::ZkResolver::BuildMethodPath("UserServiceRpc", "Login") ==
         "/UserServiceRpc/Login");
  assert(hxrpc::ZkResolver::BuildInstancePath("UserServiceRpc", "Login",
                                              "127.0.0.1:8000") ==
         "/UserServiceRpc/Login/127.0.0.1:8000");

  const auto endpoint = hxrpc::ZkResolver::ParseEndpoint("127.0.0.1:8000");
  assert(endpoint.has_value());
  assert(endpoint->port == 8000);

  return 0;
}
