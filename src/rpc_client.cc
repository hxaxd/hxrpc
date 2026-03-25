#include "rpc_client.h"
#include "codec.h"
#include "logger.h"

namespace hxrpc {

RpcClient::RpcClient(ClientConfig config)
    : config_(std::move(config)),
      resolver_(DiscoveryFactory::CreateResolver(config_.discovery)),
      transport_(ClientTransportFactory::Create()),
      serializer_(SerializerFactory::Create(config_.serialization)),
      message_pool_(std::make_shared<MessagePool>()) {}

std::expected<void, RpcError>
RpcClient::Invoke(const google::protobuf::MethodDescriptor *method,
                  const google::protobuf::Message &request,
                  google::protobuf::Message &response) {
  return Invoke(method, request, response, config_.call_options);
}

std::expected<void, RpcError>
RpcClient::Invoke(const google::protobuf::MethodDescriptor *method,
                  const google::protobuf::Message &request,
                  google::protobuf::Message &response,
                  const CallOptions &options) {
  return InvokeAsync(method, request, response, options).Get();
}

Task<std::expected<void, RpcError>>
RpcClient::InvokeAsync(const google::protobuf::MethodDescriptor *method,
                       const google::protobuf::Message &request,
                       google::protobuf::Message &response) {
  co_return co_await InvokeAsync(method, request, response, config_.call_options);
}

Task<std::expected<void, RpcError>>
RpcClient::InvokeAsync(const google::protobuf::MethodDescriptor *method,
                       const google::protobuf::Message &request,
                       google::protobuf::Message &response, CallOptions options) {
  const std::string service_name = method->service()->name();
  const std::string method_name = method->name();
  SerializationContext context{service_name, method_name};

  auto instance_result = resolver_->Resolve(service_name, method_name);
  if (!instance_result) {
    LOG(Warn) << "client resolve failed service=" << service_name
              << " method=" << method_name
              << " code=" << static_cast<int>(instance_result.error().code)
              << " message=" << instance_result.error().message;
    co_return std::unexpected(instance_result.error());
  }

  auto payload_result = serializer_->Serialize(request, context);
  if (!payload_result) {
    LOG(Warn) << "client serialize failed service=" << service_name
              << " method=" << method_name
              << " message=" << payload_result.error().message;
    co_return std::unexpected(payload_result.error());
  }

  RpcRequest rpc_request{next_request_id_.fetch_add(1), service_name, method_name,
                         options.metadata, payload_result.value()};
  LOG(Info) << "client request request_id=" << rpc_request.request_id
            << " service=" << service_name << " method=" << method_name
            << " endpoint=" << instance_result->endpoint.ToString();
  auto frame_result = RpcCodec::EncodeRequest(rpc_request);
  if (!frame_result) {
    LOG(Warn) << "client encode request failed service=" << service_name
              << " method=" << method_name
              << " message=" << frame_result.error().message;
    co_return std::unexpected(frame_result.error());
  }

  auto response_frame =
      co_await transport_->RoundTripAsync(instance_result->endpoint,
                                          frame_result.value(), options);
  if (!response_frame) {
    LOG(Warn) << "client round trip failed service=" << service_name
              << " method=" << method_name
              << " code=" << static_cast<int>(response_frame.error().code)
              << " message=" << response_frame.error().message;
    co_return std::unexpected(response_frame.error());
  }

  auto rpc_response = RpcCodec::DecodeResponse(response_frame.value());
  if (!rpc_response) {
    LOG(Warn) << "client decode response failed service=" << service_name
              << " method=" << method_name
              << " message=" << rpc_response.error().message;
    co_return std::unexpected(rpc_response.error());
  }
  LOG(Info) << "client response request_id=" << rpc_response->request_id
            << " service=" << service_name << " method=" << method_name
            << " status=" << static_cast<int>(rpc_response->status);
  if (!rpc_response->Ok()) {
    LOG(Warn) << "client response returned error service=" << service_name
              << " method=" << method_name
              << " status=" << static_cast<int>(rpc_response->status)
              << " message=" << rpc_response->error_text;
    co_return std::unexpected(
        RpcError{rpc_response->status, rpc_response->error_text});
  }

  auto pooled_response = message_pool_->Acquire(response);
  auto deserialize_result =
      serializer_->Deserialize(rpc_response->payload, *pooled_response, context);
  if (!deserialize_result) {
    LOG(Warn) << "client deserialize failed service=" << service_name
              << " method=" << method_name
              << " message=" << deserialize_result.error().message;
    co_return std::unexpected(deserialize_result.error());
  }
  response.CopyFrom(*pooled_response);
  co_return std::expected<void, RpcError>{};
}

} // namespace hxrpc
