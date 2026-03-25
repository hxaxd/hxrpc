#include "settings.h"
#include <sstream>

namespace {

std::vector<std::string> Split(std::string_view raw, char delimiter) {
  std::vector<std::string> parts;
  std::size_t cursor = 0;
  while (cursor <= raw.size()) {
    const auto next = raw.find(delimiter, cursor);
    const auto size =
        next == std::string_view::npos ? raw.size() - cursor : next - cursor;
    parts.emplace_back(raw.substr(cursor, size));
    if (next == std::string_view::npos) {
      break;
    }
    cursor = next + 1;
  }
  return parts;
}

std::string TrimCopy(std::string value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(begin, end - begin + 1);
}

} // namespace

const std::unordered_map<std::string, std::string> &hxrpcconfig::Entries() const {
  return config_map_;
}

namespace hxrpc {

std::expected<ServerConfig, std::string>
SettingsLoader::LoadServerConfig(const hxrpcconfig &config) {
  auto endpoint_result = ParseEndpoint(
      config.Load("server.host") + ":" + config.Load("server.port"),
      "rpcserver");
  if (!endpoint_result) {
    return std::unexpected(endpoint_result.error());
  }

  ServerConfig server_config;
  server_config.listen_endpoint = endpoint_result.value();
  server_config.discovery = LoadDiscoveryConfig(config);
  server_config.reactor.max_events = 128;
  server_config.serialization.backend = SerializationBackend::kProtobuf;
  return server_config;
}

std::expected<ClientConfig, std::string>
SettingsLoader::LoadClientConfig(const hxrpcconfig &config) {
  ClientConfig client_config;
  client_config.discovery = LoadDiscoveryConfig(config);
  client_config.serialization.backend = SerializationBackend::kProtobuf;
  const auto timeout_value = config.Load("client.rpc_timeout_ms");
  if (!timeout_value.empty()) {
    try {
      client_config.call_options.timeout_ms = std::stoi(timeout_value);
    } catch (...) {
      return std::unexpected("rpc_timeout_ms is invalid");
    }
    if (client_config.call_options.timeout_ms <= 0) {
      return std::unexpected("rpc_timeout_ms must be positive");
    }
  }
  return client_config;
}

std::expected<Endpoint, std::string>
SettingsLoader::ParseEndpoint(std::string_view raw, std::string_view field_name) {
  const auto separator = raw.find(':');
  if (separator == std::string_view::npos) {
    return std::unexpected(std::string(field_name) + " must be host:port");
  }

  Endpoint endpoint;
  endpoint.host = TrimCopy(std::string(raw.substr(0, separator)));
  try {
    endpoint.port = static_cast<std::uint16_t>(
        std::stoul(TrimCopy(std::string(raw.substr(separator + 1)))));
  } catch (...) {
    return std::unexpected(std::string(field_name) + " port is invalid");
  }

  if (!endpoint.IsValid()) {
    return std::unexpected(std::string(field_name) + " is incomplete");
  }
  return endpoint;
}

DiscoveryBackend SettingsLoader::ParseDiscoveryBackend(std::string_view raw) {
  if (raw == "zookeeper" || raw == "zk") {
    return DiscoveryBackend::kZookeeper;
  }
  return DiscoveryBackend::kStatic;
}

std::expected<std::vector<Endpoint>, std::string>
SettingsLoader::ParseEndpointList(std::string_view raw, std::string_view field_name) {
  std::vector<Endpoint> endpoints;
  for (auto item : Split(raw, ',')) {
    const auto parsed = ParseEndpoint(TrimCopy(std::move(item)), field_name);
    if (!parsed) {
      return std::unexpected(parsed.error());
    }
    endpoints.push_back(parsed.value());
  }
  return endpoints;
}

DiscoveryConfig SettingsLoader::LoadDiscoveryConfig(const hxrpcconfig &config) {
  DiscoveryConfig discovery_config;
  discovery_config.backend = ParseDiscoveryBackend(config.Load("discovery.backend"));

  const auto zk_endpoint = ParseEndpoint(
      config.Load("discovery.zookeeper.host") + ":" +
          config.Load("discovery.zookeeper.port"),
      "zookeeper");
  if (zk_endpoint) {
    discovery_config.zookeeper_endpoint = zk_endpoint.value();
  }

  for (const auto &[key, value] : config.Entries()) {
    if (!key.starts_with("discovery.services.")) {
      continue;
    }
    std::string service_key =
        key.substr(std::string_view("discovery.services.").size());
    auto endpoints = ParseEndpointList(value, key);
    if (!endpoints) {
      continue;
    }
    discovery_config.static_services.emplace(std::move(service_key),
                                             std::move(endpoints.value()));
  }

  return discovery_config;
}

} // namespace hxrpc
