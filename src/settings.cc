// src/settings.cc
// 强类型配置装载实现
// 关键流程: 从配置文件路径加载扁平键值 -> 解析 endpoint / 发现后端 / 日志选项
// -> 组装 ServerConfig / ClientConfig / LoggerConfig

#include "settings.h"

#include "config.h"
#include "string_utils.h"

namespace hxrpc {

namespace {

std::expected<hxrpcconfig, std::string> LoadRawConfig(std::string_view path) {
  if (path.empty()) {
    return std::unexpected("config path is empty");
  }

  hxrpcconfig config;
  auto load_result = config.LoadConfigFile(std::string(path).c_str());
  if (!load_result) {
    return std::unexpected(load_result.error());
  }
  return config;
}

std::expected<Endpoint, std::string> ParseEndpoint(std::string_view raw,
                                                   std::string_view field_name) {
  const auto separator = raw.find(':');
  if (separator == std::string_view::npos) {
    return std::unexpected(std::string(field_name) + " must be host:port");
  }

  Endpoint endpoint;
  endpoint.host = detail::TrimCopy(std::string(raw.substr(0, separator)));
  try {
    endpoint.port = static_cast<std::uint16_t>(
        std::stoul(detail::TrimCopy(std::string(raw.substr(separator + 1)))));
  } catch (...) {
    return std::unexpected(std::string(field_name) + " port is invalid");
  }

  if (!endpoint.IsValid()) {
    return std::unexpected(std::string(field_name) + " is incomplete");
  }
  return endpoint;
}

DiscoveryBackend ParseDiscoveryBackend(std::string_view raw) {
  if (raw == "zookeeper" || raw == "zk") {
    return DiscoveryBackend::kZookeeper;
  }
  return DiscoveryBackend::kStatic;
}

std::expected<LoggerMode, std::string> ParseLoggerMode(std::string_view raw) {
  if (raw == "sync") {
    return LoggerMode::kSync;
  }
  if (raw == "async") {
    return LoggerMode::kAsync;
  }
  return std::unexpected("logging.mode must be 'sync' or 'async'");
}

std::expected<LoggerSink, std::string> ParseLoggerSink(std::string_view raw) {
  if (raw == "stderr") {
    return LoggerSink::kStderr;
  }
  if (raw == "file") {
    return LoggerSink::kFile;
  }
  if (raw == "stderr_and_file") {
    return LoggerSink::kStderrAndFile;
  }
  return std::unexpected(
      "logging.sink must be 'stderr', 'file', or 'stderr_and_file'");
}

std::expected<LogLevel, std::string> ParseLogLevel(std::string_view raw) {
  if (raw == "info") {
    return LogLevel::kInfo;
  }
  if (raw == "warn") {
    return LogLevel::kWarn;
  }
  if (raw == "error") {
    return LogLevel::kError;
  }
  return std::unexpected("logging.min_level must be 'info', 'warn', or 'error'");
}

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

std::expected<std::vector<Endpoint>, std::string> ParseEndpointList(
    std::string_view raw, std::string_view field_name) {
  std::vector<Endpoint> endpoints;
  for (auto item : Split(raw, ',')) {
    const auto parsed =
        ParseEndpoint(detail::TrimCopy(std::move(item)), field_name);
    if (!parsed) {
      return std::unexpected(parsed.error());
    }
    endpoints.push_back(parsed.value());
  }
  return endpoints;
}

DiscoveryConfig LoadDiscoveryConfig(const hxrpcconfig& config) {
  DiscoveryConfig discovery_config;
  discovery_config.backend =
      ParseDiscoveryBackend(config.Load("discovery.backend"));

  const auto zk_endpoint =
      ParseEndpoint(config.Load("discovery.zookeeper.host") + ":" +
                        config.Load("discovery.zookeeper.port"),
                    "zookeeper");
  if (zk_endpoint) {
    discovery_config.zookeeper_endpoint = zk_endpoint.value();
  }

  for (const auto& [key, value] : config.Entries()) {
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

std::expected<ServerConfig, std::string> BuildServerConfig(
    const hxrpcconfig& config) {
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

std::expected<ClientConfig, std::string> BuildClientConfig(
    const hxrpcconfig& config) {
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

std::expected<LoggerConfig, std::string> BuildLoggerConfig(
    const hxrpcconfig& config) {
  LoggerConfig logger_config;

  const auto mode_result = ParseLoggerMode(config.Load("logging.mode"));
  if (!mode_result) {
    return std::unexpected(mode_result.error());
  }
  logger_config.mode = mode_result.value();

  const auto sink_result = ParseLoggerSink(config.Load("logging.sink"));
  if (!sink_result) {
    return std::unexpected(sink_result.error());
  }
  logger_config.sink = sink_result.value();

  const auto level_result = ParseLogLevel(config.Load("logging.min_level"));
  if (!level_result) {
    return std::unexpected(level_result.error());
  }
  logger_config.min_level = level_result.value();
  logger_config.file_path = config.Load("logging.file_path");

  const bool sink_writes_file = logger_config.sink == LoggerSink::kFile ||
                                logger_config.sink == LoggerSink::kStderrAndFile;
  if (sink_writes_file && logger_config.file_path.empty()) {
    return std::unexpected(
        "logging.file_path is required when logging.sink writes to file");
  }
  if (!sink_writes_file && !logger_config.file_path.empty()) {
    return std::unexpected(
        "logging.file_path must be empty when logging.sink does not write to "
        "file");
  }

  return logger_config;
}

}  // namespace

std::expected<ServerSettings, std::string> ServerSettings::Load(
    std::string_view path) {
  auto raw_config = LoadRawConfig(path);
  if (!raw_config) {
    return std::unexpected(raw_config.error());
  }

  auto config = BuildServerConfig(raw_config.value());
  if (!config) {
    return std::unexpected(config.error());
  }
  return ServerSettings{config.value()};
}

std::expected<ClientSettings, std::string> ClientSettings::Load(
    std::string_view path) {
  auto raw_config = LoadRawConfig(path);
  if (!raw_config) {
    return std::unexpected(raw_config.error());
  }

  auto config = BuildClientConfig(raw_config.value());
  if (!config) {
    return std::unexpected(config.error());
  }
  return ClientSettings{config.value()};
}

std::expected<LoggerSettings, std::string> LoggerSettings::Load(
    std::string_view path) {
  auto raw_config = LoadRawConfig(path);
  if (!raw_config) {
    return std::unexpected(raw_config.error());
  }

  auto config = BuildLoggerConfig(raw_config.value());
  if (!config) {
    return std::unexpected(config.error());
  }
  return LoggerSettings{config.value()};
}

}  // namespace hxrpc
