#include "../user.pb.h"
#include "application.h"
#include "rpc_server.h"
#include "settings.h"
#include "logger.h"
#include <cstdlib>
#include <iostream>
#include <string>
#include <sys/resource.h>

class UserService final : public Kuser::UserServiceRpc {
public:
  // 这个业务类刻意保持很小，方便教学时把注意力放在 RPC 框架本身：
  // provider 如何注册服务、框架如何解码请求、protobuf service 如何被回调。
  bool Login(const std::string &name, const std::string &password) {
    return name == "alice" && password == "123456";
  }

  bool Register(std::uint32_t id, const std::string &name,
                const std::string &password) {
    return id > 0 && !name.empty() && !password.empty();
  }

  void Login(::google::protobuf::RpcController *controller,
             const ::Kuser::LoginRequest *request,
             ::Kuser::LoginResponse *response,
             ::google::protobuf::Closure *done) override {
    (void)controller;

    const bool login_ok = Login(request->name(), request->pwd());
    auto *result = response->mutable_result();
    result->set_errcode(login_ok ? 0 : 1);
    result->set_errmsg(login_ok ? "" : "invalid username or password");
    response->set_success(login_ok);
    done->Run();
  }

  void Register(::google::protobuf::RpcController *controller,
                const ::Kuser::RegisterRequest *request,
                ::Kuser::RegisterResponse *response,
                ::google::protobuf::Closure *done) override {
    (void)controller;

    const bool register_ok =
        Register(request->id(), request->name(), request->pwd());
    auto *result = response->mutable_result();
    result->set_errcode(register_ok ? 0 : 1);
    result->set_errmsg(register_ok ? "" : "register request is invalid");
    response->set_success(register_ok);
    done->Run();
  }
};

int main(int argc, char **argv) {
  rlimit core_limit{};
  core_limit.rlim_cur = 0;
  core_limit.rlim_max = 0;
  (void)::setrlimit(RLIMIT_CORE, &core_limit);

  hxrpcApplication::Init(argc, argv);
  hxrpc::LoggerOptions server_logger_options;
  server_logger_options.async_enabled = true;
  server_logger_options.stderr_enabled = true;
  server_logger_options.file_path = "logs/benchmark_server.log";
  server_logger_options.min_level = hxrpc::LogLevel::kWarn;
  if (auto logger_result = hxrpc::Logger::Instance().Configure(server_logger_options);
      !logger_result) {
    LOG(Warn) << "failed to configure benchmark server logger: "
              << logger_result.error();
  }

  auto config_result =
      hxrpc::SettingsLoader::LoadServerConfig(hxrpcApplication::GetConfig());
  if (!config_result) {
    LOG(Error) << "failed to build server config: " << config_result.error();
    return EXIT_FAILURE;
  }

  hxrpc::RpcServer server(config_result.value());
  server.RegisterService(new UserService());

  std::cout << "Starting benchmark RPC server. Login with alice / 123456."
            << std::endl;
  server.Run();
  return 0;
}
