#include "task.h"
#include "test_support.h"
#include <cassert>

namespace {

hxrpc::Task<std::expected<bool, hxrpc::RpcError>>
RunAsyncLogin(hxrpc::RpcClient &client,
              const google::protobuf::MethodDescriptor *method) {
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

} // namespace

int main() {
  const auto port = hxrpc::test::PickFreePort();
  auto server_config = hxrpc::test::MakeServerConfig(port);
  hxrpc::RpcServer server(server_config);
  hxrpc::test::DemoUserService service;
  auto server_thread = hxrpc::test::StartServer(server, service);
  hxrpc::test::WaitForServerReady();

  auto client_config = hxrpc::test::MakeClientConfig(port);
  hxrpc::RpcClient client(client_config);
  const auto *login_method = hxrpc::test::FindMethod("Login");
  assert(login_method != nullptr);

  const auto result = RunAsyncLogin(client, login_method).Get();
  assert(result.has_value());
  assert(result.value());

  server.Stop();
  server_thread.join();
  return 0;
}
