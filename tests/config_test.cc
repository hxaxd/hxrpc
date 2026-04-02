#include <cassert>
#include <filesystem>
#include <fstream>

#include "settings.h"

int main() {
  // 测试目的: 从临时 YAML 文件加载 typed settings, 并验证关键字段映射
  const auto yaml_path =
      std::filesystem::temp_directory_path() / "hxrpc_config_test.yaml";
  const auto invalid_timeout_path =
      std::filesystem::temp_directory_path() / "hxrpc_invalid_timeout.yaml";
  const auto invalid_logger_path =
      std::filesystem::temp_directory_path() / "hxrpc_invalid_logger.yaml";
  const auto invalid_server_path =
      std::filesystem::temp_directory_path() / "hxrpc_invalid_server.yaml";

  {
    // 构造最小可用配置, 覆盖 server/client/discovery/logging 四类配置段
    std::ofstream yaml_file(yaml_path);
    yaml_file << "server:\n";
    yaml_file << "  host: 127.0.0.1\n";
    yaml_file << "  port: 9000\n";
    yaml_file << "client:\n";
    yaml_file << "  rpc_timeout_ms: 1800\n";
    yaml_file << "discovery:\n";
    yaml_file << "  backend: static\n";
    yaml_file << "  services:\n";
    yaml_file << "    UserServiceRpc.Login: 127.0.0.1:9000\n";
    yaml_file << "    UserServiceRpc.Register: 127.0.0.1:9000\n";
    yaml_file << "logging:\n";
    yaml_file << "  mode: async\n";
    yaml_file << "  sink: stderr_and_file\n";
    yaml_file << "  file_path: logs/test.log\n";
    yaml_file << "  min_level: warn\n";
  }

  const auto yaml_server_settings =
      hxrpc::ServerSettings::Load(yaml_path.string());
  assert(yaml_server_settings.has_value());
  // 关键断言: 端口与静态服务映射被正确解析
  assert(yaml_server_settings->config.listen_endpoint.port == 9000);
  assert(yaml_server_settings->config.discovery.static_services.contains(
      "UserServiceRpc.Login"));

  const auto yaml_client_settings =
      hxrpc::ClientSettings::Load(yaml_path.string());
  assert(yaml_client_settings.has_value());
  // 关键断言: 客户端超时配置映射正确
  assert(yaml_client_settings->config.call_options.timeout_ms == 1800);

  const auto logger_settings = hxrpc::LoggerSettings::Load(yaml_path.string());
  assert(logger_settings.has_value());
  assert(logger_settings->config.mode == hxrpc::LoggerMode::kAsync);
  assert(logger_settings->config.sink == hxrpc::LoggerSink::kStderrAndFile);
  assert(logger_settings->config.file_path == "logs/test.log");
  assert(logger_settings->config.min_level == hxrpc::LogLevel::kWarn);

  const auto missing_file_result =
      hxrpc::ServerSettings::Load("missing-config.yaml");
  assert(!missing_file_result.has_value());

  {
    std::ofstream yaml_file(invalid_timeout_path);
    yaml_file << "client:\n";
    yaml_file << "  rpc_timeout_ms: invalid\n";
    yaml_file << "discovery:\n";
    yaml_file << "  backend: static\n";
  }
  const auto invalid_timeout_settings =
      hxrpc::ClientSettings::Load(invalid_timeout_path.string());
  assert(!invalid_timeout_settings.has_value());

  {
    std::ofstream yaml_file(invalid_logger_path);
    yaml_file << "logging:\n";
    yaml_file << "  mode: async\n";
    yaml_file << "  sink: file\n";
    yaml_file << "  min_level: warn\n";
  }
  const auto invalid_logger_settings =
      hxrpc::LoggerSettings::Load(invalid_logger_path.string());
  assert(!invalid_logger_settings.has_value());

  {
    std::ofstream yaml_file(invalid_server_path);
    yaml_file << "server:\n";
    yaml_file << "  host: 127.0.0.1\n";
    yaml_file << "  port: invalid\n";
    yaml_file << "discovery:\n";
    yaml_file << "  backend: static\n";
  }
  const auto invalid_server_settings =
      hxrpc::ServerSettings::Load(invalid_server_path.string());
  assert(!invalid_server_settings.has_value());

  std::filesystem::remove(yaml_path);
  std::filesystem::remove(invalid_timeout_path);
  std::filesystem::remove(invalid_logger_path);
  std::filesystem::remove(invalid_server_path);
  return 0;
}
