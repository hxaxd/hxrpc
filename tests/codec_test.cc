#include "codec.h"

#include <cassert>

int main() {
  // 覆盖场景: 请求帧的编码->解码回环, 确保字段完整保真
  hxrpc::RpcRequest request{42, "UserServiceRpc", "Login", "meta", "payload"};
  const auto encoded_request = hxrpc::RpcCodec::EncodeRequest(request);
  assert(encoded_request.has_value());

  const auto decoded_request =
      hxrpc::RpcCodec::DecodeRequest(encoded_request.value());
  assert(decoded_request.has_value());
  assert(decoded_request->request_id == 42);
  assert(decoded_request->service_name == "UserServiceRpc");
  assert(decoded_request->method_name == "Login");
  assert(decoded_request->metadata == "meta");
  assert(decoded_request->payload == "payload");

  // 覆盖场景: 响应帧的编码->解码回环, 验证状态码与载荷不丢失
  hxrpc::RpcResponse response{42, hxrpc::RpcStatusCode::kOk, "", "hello"};
  const auto encoded_response = hxrpc::RpcCodec::EncodeResponse(response);
  assert(encoded_response.has_value());

  const auto decoded_response =
      hxrpc::RpcCodec::DecodeResponse(encoded_response.value());
  assert(decoded_response.has_value());
  // 关键断言: request_id 必须与请求一致, 便于客户端按请求关联响应
  assert(decoded_response->request_id == 42);
  // 关键断言: 协议层状态应为成功 (与业务字段语义分离)
  assert(decoded_response->Ok());
  assert(decoded_response->payload == "hello");

  return 0;
}
