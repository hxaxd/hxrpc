#ifndef HXRPC_SETTINGS_H
#define HXRPC_SETTINGS_H

#include "config.h"
#include "types.h"
#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

namespace hxrpc {

enum class DiscoveryBackend {
  kStatic,
  kZookeeper,
};

enum class SerializationBackend {
  kProtobuf,
};

struct ReactorConfig {
  int max_events{128};
};

struct DiscoveryConfig {
  DiscoveryBackend backend{DiscoveryBackend::kStatic};
  Endpoint zookeeper_endpoint;
  ResolverSelectionPolicy selection_policy{ResolverSelectionPolicy::kRandom};
  std::unordered_map<std::string, std::vector<Endpoint>> static_services;
};

struct SerializationConfig {
  SerializationBackend backend{SerializationBackend::kProtobuf};
};

struct ServerConfig {
  Endpoint listen_endpoint;
  ReactorConfig reactor;
  DiscoveryConfig discovery;
  SerializationConfig serialization;
};

struct ClientConfig {
  DiscoveryConfig discovery;
  SerializationConfig serialization;
  CallOptions call_options;
};

class SettingsLoader {
public:
  [[nodiscard]] static std::expected<ServerConfig, std::string>
  LoadServerConfig(const hxrpcconfig &config);
  [[nodiscard]] static std::expected<ClientConfig, std::string>
  LoadClientConfig(const hxrpcconfig &config);

private:
  [[nodiscard]] static std::expected<Endpoint, std::string>
  ParseEndpoint(std::string_view raw, std::string_view field_name);
  [[nodiscard]] static DiscoveryBackend
  ParseDiscoveryBackend(std::string_view raw);
  [[nodiscard]] static std::expected<std::vector<Endpoint>, std::string>
  ParseEndpointList(std::string_view raw, std::string_view field_name);
  [[nodiscard]] static DiscoveryConfig LoadDiscoveryConfig(const hxrpcconfig &config);
};

} // namespace hxrpc

#endif
