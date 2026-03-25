#include "controller.h"
#include "test_support.h"
#include <cassert>

namespace {

class MetadataAwareUserService final : public Kuser::UserServiceRpc {
public:
  void Login(::google::protobuf::RpcController *controller,
             const ::Kuser::LoginRequest *request,
             ::Kuser::LoginResponse *response,
             ::google::protobuf::Closure *done) override {
    (void)request;
    auto *typed = dynamic_cast<hxrpccontroller *>(controller);
    assert(typed != nullptr);
    auto *result = response->mutable_result();
    result->set_errcode(0);
    result->set_errmsg(typed->RequestMetadata());
    response->set_success(typed->RequestMetadata() == "trace_id=metadata-test");
    done->Run();
  }

  void Register(::google::protobuf::RpcController *controller,
                const ::Kuser::RegisterRequest *request,
                ::Kuser::RegisterResponse *response,
                ::google::protobuf::Closure *done) override {
    (void)controller;
    (void)request;
    response->set_success(true);
    done->Run();
  }
};

} // namespace

int main() {
  const auto port = hxrpc::test::PickFreePort();
  auto server_config = hxrpc::test::MakeServerConfig(port);
  hxrpc::RpcServer server(server_config);
  MetadataAwareUserService service;
  server.RegisterService(&service);
  auto server_thread = std::thread([&server]() { server.Run(); });
  hxrpc::test::WaitForServerReady();

  auto client_config = hxrpc::test::MakeClientConfig(port);
  hxrpc::RpcClient client(client_config);
  const auto *login_method = hxrpc::test::FindMethod("Login");
  assert(login_method != nullptr);

  Kuser::LoginRequest request;
  request.set_name("ignored");
  request.set_pwd("ignored");
  Kuser::LoginResponse response;
  hxrpc::CallOptions options = client_config.call_options;
  options.metadata = "trace_id=metadata-test";
  const auto result = client.Invoke(login_method, request, response, options);
  assert(result.has_value());
  assert(response.success());
  assert(response.result().errmsg() == "trace_id=metadata-test");

  server.Stop();
  server_thread.join();
  return 0;
}
