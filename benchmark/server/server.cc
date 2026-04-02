// benchmark/server/server.cc
// 基准压测服务端示例
// 设计目标: 提供最小可运行的 RPC Provider, 实现 Login/Register 两个方法,
// 与 benchmark 客户端形成端到端压测闭环

#include <sys/resource.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "../user.pb.h"
#include "logger.h"
#include "rpc_server.h"
#include "settings.h"

namespace {

constexpr std::string_view kConfigPath = "server.yaml";

}

class UserService final : public Kuser::UserServiceRpc {
 public:
  // 业务判定函数 (非 RPC 回调)
  // 参数: name/password - 用户名与密码
  // 返回: 认证是否通过
  // 设计原因: 将纯业务规则与 RPC 编解码解耦, 便于在教学/测试中替换策略
  // 这个业务类刻意保持很小, 方便教学时把注意力放在 RPC 框架本身:
  // provider 如何注册服务, 框架如何解码请求, protobuf service 如何被回调
  bool Login(const std::string& name, const std::string& password) {
    return name == "alice" && password == "123456";
  }

  // 业务注册判定函数 (非 RPC 回调)
  // 参数: id/name/password - 注册请求字段
  // 返回: 请求是否满足最小合法性校验
  bool Register(std::uint32_t id, const std::string& name,
                const std::string& password) {
    return id > 0 && !name.empty() && !password.empty();
  }

  // Login RPC 回调
  // 参数:
  //   - controller: 框架传入控制器 (本示例未使用)
  //   - request: 输入请求
  //   - response: 输出响应
  //   - done: 完成回调, 必须调用以通知框架结束本次处理
  // 返回: void (结果通过 response 输出)
  // 错误语义: 业务失败通过 result.errcode/errmsg 与 success=false
  // 回传, 不抛异常
  void Login(::google::protobuf::RpcController* controller,
             const ::Kuser::LoginRequest* request,
             ::Kuser::LoginResponse* response,
             ::google::protobuf::Closure* done) override {
    (void)controller;

    const bool login_ok = Login(request->name(), request->pwd());
    auto* result = response->mutable_result();
    result->set_errcode(login_ok ? 0 : 1);
    result->set_errmsg(login_ok ? "" : "invalid username or password");
    response->set_success(login_ok);
    done->Run();
  }

  // Register RPC 回调
  // 参数与返回语义同 Login错误通过 response 字段表达
  void Register(::google::protobuf::RpcController* controller,
                const ::Kuser::RegisterRequest* request,
                ::Kuser::RegisterResponse* response,
                ::google::protobuf::Closure* done) override {
    (void)controller;

    const bool register_ok =
        Register(request->id(), request->name(), request->pwd());
    auto* result = response->mutable_result();
    result->set_errcode(register_ok ? 0 : 1);
    result->set_errmsg(register_ok ? "" : "register request is invalid");
    response->set_success(register_ok);
    done->Run();
  }
};

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  // 设计原因: 示例服务默认作为长期运行进程, 关闭 core dump
  // 可降低压测环境的磁盘占用
  rlimit core_limit{};
  core_limit.rlim_cur = 0;
  core_limit.rlim_max = 0;
  (void)::setrlimit(RLIMIT_CORE, &core_limit);

  auto logger_settings = hxrpc::LoggerSettings::Load(kConfigPath);
  if (!logger_settings) {
    std::cerr << "failed to load logger settings: " << logger_settings.error()
              << std::endl;
    return EXIT_FAILURE;
  }
  if (auto logger_result =
          hxrpc::Logger::Instance().Configure(logger_settings->config.ToOptions());
      !logger_result) {
    std::cerr << "failed to configure logger: " << logger_result.error()
              << std::endl;
    return EXIT_FAILURE;
  }

  auto server_settings = hxrpc::ServerSettings::Load(kConfigPath);
  if (!server_settings) {
    LOG(Error) << "failed to load server settings: " << server_settings.error();
    return EXIT_FAILURE;
  }

  hxrpc::RpcServer server(server_settings->config);
  server.RegisterService(new UserService());

  std::cout << "Starting benchmark RPC server. Login with alice / 123456."
            << std::endl;
  server.Run();
  return 0;
}
