#include "types.h"

namespace hxrpc {

std::string Endpoint::ToString() const {
  return host + ":" + std::to_string(port);
}

} // namespace hxrpc
