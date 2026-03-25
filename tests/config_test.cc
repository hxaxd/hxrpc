#include "config.h"
#include "settings.h"
#include <cassert>
#include <filesystem>
#include <fstream>

int main() {
  const auto yaml_path =
      std::filesystem::temp_directory_path() / "hxrpc_config_test.yaml";

  {
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
    yaml_file << "  async: true\n";
    yaml_file << "  to_stderr: true\n";
    yaml_file << "  path: logs/test.log\n";
  }

  hxrpcconfig yaml_config;
  const auto yaml_result = yaml_config.LoadConfigFile(yaml_path.string().c_str());
  assert(yaml_result.has_value());
  assert(yaml_config.Load("server.host") == "127.0.0.1");
  assert(yaml_config.Load("logging.path") == "logs/test.log");
  assert(yaml_config.Load("missing_key").empty());
  assert(!yaml_config.Entries().empty());

  const auto yaml_server_config = hxrpc::SettingsLoader::LoadServerConfig(yaml_config);
  assert(yaml_server_config.has_value());
  assert(yaml_server_config->listen_endpoint.port == 9000);
  assert(yaml_server_config->discovery.static_services.contains("UserServiceRpc.Login"));
  const auto yaml_client_config = hxrpc::SettingsLoader::LoadClientConfig(yaml_config);
  assert(yaml_client_config.has_value());
  assert(yaml_client_config->call_options.timeout_ms == 1800);

  std::filesystem::remove(yaml_path);
  return 0;
}
