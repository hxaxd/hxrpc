#include <cassert>

#include "../benchmark/user.pb.h"
#include "message_pool.h"
#include "rpc_dispatcher.h"

namespace {

class TestUserService final : public Kuser::UserServiceRpc {
 public:
  void Login(::google::protobuf::RpcController* controller,
             const ::Kuser::LoginRequest* request,
             ::Kuser::LoginResponse* response,
             ::google::protobuf::Closure* done) override {
    (void)controller;
    auto* result = response->mutable_result();
    result->set_errcode(0);
    result->set_errmsg("");
    response->set_success(request->name() == "alice");
    done->Run();
  }

  void Register(::google::protobuf::RpcController* controller,
                const ::Kuser::RegisterRequest* request,
                ::Kuser::RegisterResponse* response,
                ::google::protobuf::Closure* done) override {
    (void)controller;
    auto* result = response->mutable_result();
    result->set_errcode(0);
    result->set_errmsg("");
    response->set_success(request->id() > 0);
    done->Run();
  }
};

}  // namespace

int main() {
  // 测试目的: 验证 RpcDispatcher 对已注册服务的解码->调用->编码完整分发链路
  TestUserService service;
  hxrpc::ServiceRegistry registry;
  registry.RegisterService(&service);
  auto serializer =
      hxrpc::SerializerFactory::Create(hxrpc::SerializationConfig{});
  auto message_pool = std::make_shared<hxrpc::MessagePool>();
  hxrpc::RpcDispatcher dispatcher(registry, serializer, message_pool);

  // 构造一个 Login 请求并序列化为协议 payload
  Kuser::LoginRequest request_message;
  request_message.set_name("alice");
  request_message.set_pwd("123456");
  auto payload = serializer->Serialize(
      request_message, hxrpc::SerializationContext{"UserServiceRpc", "Login"});
  assert(payload.has_value());

  auto request_frame = hxrpc::RpcCodec::EncodeRequest(
      hxrpc::RpcRequest{1, "UserServiceRpc", "Login", {}, payload.value()});
  assert(request_frame.has_value());

  // 关键步骤: 由 dispatcher 处理完整帧, 得到响应帧
  auto response_frame = dispatcher.HandleFrame(request_frame.value());
  assert(response_frame.has_value());

  auto response = hxrpc::RpcCodec::DecodeResponse(response_frame.value());
  assert(response.has_value());
  // 关键断言: 协议层调用成功
  assert(response->Ok());
  Kuser::LoginResponse response_message;
  auto parse_result = serializer->Deserialize(
      response->payload, response_message,
      hxrpc::SerializationContext{"UserServiceRpc", "Login"});
  assert(parse_result.has_value());
  // 关键断言: 业务层返回 success=true, 证明目标方法被正确执行
  assert(response_message.success());

  return 0;
}
