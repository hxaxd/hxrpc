#ifndef HXRPC_TYPES_H
#define HXRPC_TYPES_H

#include <cstdint>
#include <string>

namespace hxrpc {

enum class RpcStatusCode : std::int32_t {
  kOk = 0,
  kInvalidPacket = 1,
  kUnknownService = 2,
  kUnknownMethod = 3,
  kSerializeError = 4,
  kDeserializeError = 5,
  kNetworkError = 6,
  kDiscoveryError = 7,
  kInternalError = 8,
};

struct RpcError {
  RpcStatusCode code{RpcStatusCode::kOk};
  std::string message;
};

struct CallOptions {
  int timeout_ms{3000};
  std::string metadata;
};

struct Endpoint {
  std::string host;
  std::uint16_t port{0};

  [[nodiscard]] std::string ToString() const;
  [[nodiscard]] bool IsValid() const { return !host.empty() && port != 0; }
};

struct ServiceInstance {
  std::string id;
  Endpoint endpoint;
};

enum class ResolverSelectionPolicy {
  kRandom,
};

struct RpcRequest {
  std::uint64_t request_id{0};
  std::string service_name;
  std::string method_name;
  std::string metadata;
  std::string payload;
};

struct RpcResponse {
  std::uint64_t request_id{0};
  RpcStatusCode status{RpcStatusCode::kOk};
  std::string error_text;
  std::string payload;

  [[nodiscard]] bool Ok() const { return status == RpcStatusCode::kOk; }
};

} // namespace hxrpc

#endif
