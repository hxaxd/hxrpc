#ifndef HXRPC_DISPATCHER_H
#define HXRPC_DISPATCHER_H

#include "codec.h"
#include "message_pool.h"
#include "serializer.h"
#include "service_registry.h"
#include <memory>

namespace hxrpc {

class RpcDispatcher {
public:
  RpcDispatcher(const ServiceRegistry &registry,
                std::shared_ptr<Serializer> serializer,
                std::shared_ptr<MessagePool> message_pool);

  [[nodiscard]] std::expected<std::string, RpcError>
  HandleFrame(std::string_view frame) const;

private:
  const ServiceRegistry &registry_;
  std::shared_ptr<Serializer> serializer_;
  std::shared_ptr<MessagePool> message_pool_;
};

} // namespace hxrpc

#endif
