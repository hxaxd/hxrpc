#ifndef HXRPC_STATUS_HELPER_H
#define HXRPC_STATUS_HELPER_H

#include "types.h"
#include <string_view>

namespace hxrpc {

[[nodiscard]] constexpr std::string_view ToString(RpcStatusCode code) {
  switch (code) {
  case RpcStatusCode::kOk:
    return "ok";
  case RpcStatusCode::kInvalidPacket:
    return "invalid_packet";
  case RpcStatusCode::kUnknownService:
    return "unknown_service";
  case RpcStatusCode::kUnknownMethod:
    return "unknown_method";
  case RpcStatusCode::kSerializeError:
    return "serialize_error";
  case RpcStatusCode::kDeserializeError:
    return "deserialize_error";
  case RpcStatusCode::kNetworkError:
    return "network_error";
  case RpcStatusCode::kDiscoveryError:
    return "discovery_error";
  case RpcStatusCode::kInternalError:
    return "internal_error";
  }
  return "unknown";
}

} // namespace hxrpc

#endif
