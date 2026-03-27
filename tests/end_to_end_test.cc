#include <cassert>

#include "test_support.h"

int main() {
  // 测试目的: 端到端验证 RpcClient/RpcServer
  // 在真实网络路径上的成功与失败业务结果
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

  // 场景一: 正确用户名密码, 期望业务成功且 errcode=0
  Kuser::LoginRequest good_request;
  good_request.set_name("alice");
  good_request.set_pwd("123456");
  Kuser::LoginResponse good_response;
  const auto success = client.Invoke(login_method, good_request, good_response);
  assert(success.has_value());
  assert(good_response.success());
  assert(good_response.result().errcode() == 0);

  // 场景二: 错误密码, RPC 传输仍成功, 但业务结果应失败且 errcode=1
  Kuser::LoginRequest bad_request;
  bad_request.set_name("alice");
  bad_request.set_pwd("bad-password");
  Kuser::LoginResponse bad_response;
  const auto business_result =
      client.Invoke(login_method, bad_request, bad_response);
  assert(business_result.has_value());
  assert(!bad_response.success());
  assert(bad_response.result().errcode() == 1);

  server.Stop();
  server_thread.join();
  return 0;
}
