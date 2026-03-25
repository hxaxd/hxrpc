#ifndef HXRPC_CONFIG_H
#define HXRPC_CONFIG_H

#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>

class hxrpcconfig {
public:
  [[nodiscard]] std::expected<void, std::string>
  LoadConfigFile(const char *config_file);
  [[nodiscard]] std::string Load(std::string_view key) const;
  [[nodiscard]] const std::unordered_map<std::string, std::string> &Entries() const;

  static void Trim(std::string &value);

private:
  std::unordered_map<std::string, std::string> config_map_;
};

#endif
