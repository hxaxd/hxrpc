#include "application.h"
#include "logger.h"
#include <cctype>
#include <cstdlib>

hxrpcApplication *hxrpcApplication::application_ = nullptr;
hxrpcconfig hxrpcApplication::config_{};
std::mutex hxrpcApplication::mutex_{};

namespace {

bool ParseBoolWithDefault(std::string value, bool default_value) {
  for (char &ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  if (value.empty()) {
    return default_value;
  }
  if (value == "1" || value == "true" || value == "yes" || value == "on") {
    return true;
  }
  if (value == "0" || value == "false" || value == "no" || value == "off") {
    return false;
  }
  return default_value;
}

void ConfigureLogger(const hxrpcconfig &config) {
  hxrpc::LoggerOptions options;
  options.async_enabled =
      ParseBoolWithDefault(config.Load("logging.async"), true);
  options.stderr_enabled =
      ParseBoolWithDefault(config.Load("logging.to_stderr"), true);
  options.file_path = config.Load("logging.path");

  if (auto result = hxrpc::Logger::Instance().Configure(options); !result) {
    LOG(Warn) << "failed to configure logger: " << result.error();
  }
}

} // namespace

void hxrpcApplication::Init(int argc, char **argv) {
  std::string config_file;
  for (int index = 1; index < argc; ++index) {
    const std::string arg = argv[index];
    if (arg == "-i" && index + 1 < argc) {
      config_file = argv[++index];
    }
  }

  if (config_file.empty()) {
    LOG(Error) << "missing config file, use: command -i <config-file>";
    std::exit(EXIT_FAILURE);
  }

  if (auto result = config_.LoadConfigFile(config_file.c_str()); !result) {
    LOG(Error) << "failed to load config file '" << config_file
               << "': " << result.error();
    std::exit(EXIT_FAILURE);
  }

  ConfigureLogger(config_);
}

hxrpcApplication &hxrpcApplication::GetInstance() {
  std::lock_guard<std::mutex> lock(mutex_);
  if (application_ == nullptr) {
    application_ = new hxrpcApplication();
    std::atexit(DeleteInstance);
  }
  return *application_;
}

hxrpcconfig &hxrpcApplication::GetConfig() { return config_; }

void hxrpcApplication::DeleteInstance() {
  delete application_;
  application_ = nullptr;
}
