#include <cassert>

#include "task.h"
#include "test_support.h"

namespace {

hxrpc::Task<std::expected<bool, hxrpc::RpcError>> RunAsyncLogin(
    hxrpc::RpcClient& client,
    const google::protobuf::MethodDescriptor* method) {
  // 封装一个协程调用: 将 RPC 层成功与业务层 success 布尔值分离返回
  Kuser::LoginRequest request;
  request.set_name("alice");
  request.set_pwd("123456");
  Kuser::LoginResponse response;

  auto result = co_await client.InvokeAsync(method, request, response);
  if (!result) {
    co_return std::unexpected(result.error());
  }
  co_return response.success();
}

}  // namespace

int main() {
  // 测试目的: 验证 InvokeAsync + Task::Get 协同路径可完成一次成功登录
  const auto port = hxrpc::test::PickFreePort();
  auto server_config = hxrpc::test::MakeServerConfig(port);
  hxrpc::RpcServer server(server_config);
  hxrpc::test::DemoUserService service;
  auto server_thread = hxrpc::test::StartServer(server, service);
  hxrpc::test::WaitForServerReady();

  auto client_config = hxrpc::test::MakeClientConfig(port);
  hxrpc::RpcClient client(client_config);
  const auto* login_method = hxrpc::test::FindMethod("Login");
  assert(login_method != nullptr);

  const auto result = RunAsyncLogin(client, login_method).Get();
  // 关键断言: expected 有值表示 RPC 调用层没有错误
  assert(result.has_value());
  // 关键断言: 值为 true 表示业务登录成功
  assert(result.value());

  server.Stop();
  server_thread.join();
  return 0;
}
