#include "test_support.h"
#include <cassert>

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

  Kuser::LoginRequest good_request;
  good_request.set_name("alice");
  good_request.set_pwd("123456");
  Kuser::LoginResponse good_response;
  const auto success = client.Invoke(login_method, good_request, good_response);
  assert(success.has_value());
  assert(good_response.success());
  assert(good_response.result().errcode() == 0);

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
