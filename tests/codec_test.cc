#include "codec.h"
#include <cassert>

int main() {
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

  hxrpc::RpcResponse response{42, hxrpc::RpcStatusCode::kOk, "", "hello"};
  const auto encoded_response = hxrpc::RpcCodec::EncodeResponse(response);
  assert(encoded_response.has_value());

  const auto decoded_response =
      hxrpc::RpcCodec::DecodeResponse(encoded_response.value());
  assert(decoded_response.has_value());
  assert(decoded_response->request_id == 42);
  assert(decoded_response->Ok());
  assert(decoded_response->payload == "hello");

  return 0;
}
