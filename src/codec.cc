#include "codec.h"
#include "header.pb.h"
#include <arpa/inet.h>
#include <cstring>
#include <utility>

namespace {

constexpr std::size_t kLengthPrefixSize = sizeof(std::uint32_t);

std::expected<std::string, hxrpc::RpcError>
EncodeFrame(std::string_view header, std::string_view payload) {
  const std::uint32_t frame_size = static_cast<std::uint32_t>(
      kLengthPrefixSize + header.size() + payload.size());
  const std::uint32_t network_frame_size = htonl(frame_size);
  const std::uint32_t network_header_size =
      htonl(static_cast<std::uint32_t>(header.size()));

  std::string frame;
  frame.reserve(kLengthPrefixSize * 2 + header.size() + payload.size());
  frame.append(reinterpret_cast<const char *>(&network_frame_size), kLengthPrefixSize);
  frame.append(reinterpret_cast<const char *>(&network_header_size), kLengthPrefixSize);
  frame.append(header);
  frame.append(payload);
  return frame;
}

std::expected<std::pair<std::string_view, std::string_view>, hxrpc::RpcError>
SplitFrame(std::string_view frame) {
  if (frame.size() < kLengthPrefixSize * 2) {
    return std::unexpected(
        hxrpc::RpcError{hxrpc::RpcStatusCode::kInvalidPacket, "frame is too small"});
  }

  std::uint32_t network_frame_size = 0;
  std::memcpy(&network_frame_size, frame.data(), kLengthPrefixSize);
  const std::uint32_t declared_frame_size = ntohl(network_frame_size);
  if (frame.size() != kLengthPrefixSize + declared_frame_size) {
    return std::unexpected(hxrpc::RpcError{
        hxrpc::RpcStatusCode::kInvalidPacket, "frame length prefix does not match"});
  }

  std::uint32_t network_header_size = 0;
  std::memcpy(&network_header_size, frame.data() + kLengthPrefixSize, kLengthPrefixSize);
  const std::uint32_t header_size = ntohl(network_header_size);

  if (frame.size() < kLengthPrefixSize * 2 + header_size) {
    return std::unexpected(hxrpc::RpcError{
        hxrpc::RpcStatusCode::kInvalidPacket, "header length exceeds frame size"});
  }

  return std::pair{
      frame.substr(kLengthPrefixSize * 2, header_size),
      frame.substr(kLengthPrefixSize * 2 + header_size)};
}

} // namespace

namespace hxrpc {

std::expected<std::string, RpcError>
RpcCodec::EncodeRequest(const RpcRequest &request) {
  // 请求头只放“路由和解析必须知道的信息”，业务 payload 仍保持为 protobuf。
  // 这样既能统一框架协议，又不会限制业务层继续使用 protobuf 的生成代码。
  RpcHeader header;
  header.set_request_id(request.request_id);
  header.set_service_name(request.service_name);
  header.set_method_name(request.method_name);
  header.set_args_size(static_cast<std::uint32_t>(request.payload.size()));
  header.set_meta_size(static_cast<std::uint32_t>(request.metadata.size()));

  std::string header_buffer;
  if (!header.SerializeToString(&header_buffer)) {
    return std::unexpected(
        RpcError{RpcStatusCode::kSerializeError, "failed to serialize request header"});
  }

  std::string payload = request.metadata;
  payload.append(request.payload);
  return EncodeFrame(header_buffer, payload);
}

std::expected<std::string, RpcError>
RpcCodec::EncodeResponse(const RpcResponse &response) {
  RpcResponseHeader header;
  header.set_request_id(response.request_id);
  header.set_status_code(static_cast<std::int32_t>(response.status));
  header.set_error_text(response.error_text);
  header.set_payload_size(static_cast<std::uint32_t>(response.payload.size()));

  std::string header_buffer;
  if (!header.SerializeToString(&header_buffer)) {
    return std::unexpected(RpcError{RpcStatusCode::kSerializeError,
                                    "failed to serialize response header"});
  }

  return EncodeFrame(header_buffer, response.payload);
}

std::expected<RpcRequest, RpcError> RpcCodec::DecodeRequest(std::string_view frame) {
  // Decode 阶段先验证外层包长，再解析 protobuf header，
  // 最后检查 payload 长度是否和 header 中声明的一致。
  auto split_result = SplitFrame(frame);
  if (!split_result) {
    return std::unexpected(split_result.error());
  }

  const auto [header_view, payload_view] = split_result.value();
  RpcHeader header;
  if (!header.ParseFromArray(header_view.data(), static_cast<int>(header_view.size()))) {
    return std::unexpected(
        RpcError{RpcStatusCode::kInvalidPacket, "failed to parse request header"});
  }

  const std::size_t expected_size =
      static_cast<std::size_t>(header.meta_size()) + header.args_size();
  if (payload_view.size() != expected_size) {
    return std::unexpected(
        RpcError{RpcStatusCode::kInvalidPacket, "request payload length mismatch"});
  }

  RpcRequest request;
  request.request_id = header.request_id();
  request.service_name = header.service_name();
  request.method_name = header.method_name();
  request.metadata = std::string(payload_view.substr(0, header.meta_size()));
  request.payload = std::string(payload_view.substr(header.meta_size()));
  return request;
}

std::expected<RpcResponse, RpcError>
RpcCodec::DecodeResponse(std::string_view frame) {
  auto split_result = SplitFrame(frame);
  if (!split_result) {
    return std::unexpected(split_result.error());
  }

  const auto [header_view, payload_view] = split_result.value();
  RpcResponseHeader header;
  if (!header.ParseFromArray(header_view.data(), static_cast<int>(header_view.size()))) {
    return std::unexpected(
        RpcError{RpcStatusCode::kInvalidPacket, "failed to parse response header"});
  }

  if (payload_view.size() != static_cast<std::size_t>(header.payload_size())) {
    return std::unexpected(
        RpcError{RpcStatusCode::kInvalidPacket, "response payload length mismatch"});
  }

  RpcResponse response;
  response.request_id = header.request_id();
  response.status = static_cast<RpcStatusCode>(header.status_code());
  response.error_text = header.error_text();
  response.payload = std::string(payload_view);
  return response;
}

} // namespace hxrpc
