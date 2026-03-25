#ifndef HXRPC_CLIENT_TRANSPORT_H
#define HXRPC_CLIENT_TRANSPORT_H

#include "task.h"
#include "types.h"
#include <expected>
#include <memory>
#include <string>
#include <string_view>

namespace hxrpc {

class ClientTransport {
public:
  virtual ~ClientTransport() = default;
  [[nodiscard]] virtual std::expected<std::string, RpcError>
  RoundTrip(const Endpoint &endpoint, std::string_view frame,
            const CallOptions &options) = 0;
  [[nodiscard]] virtual Task<std::expected<std::string, RpcError>>
  RoundTripAsync(const Endpoint &endpoint, std::string frame,
                 CallOptions options) = 0;
};

class TcpClientTransport final : public ClientTransport {
public:
  [[nodiscard]] std::expected<std::string, RpcError>
  RoundTrip(const Endpoint &endpoint, std::string_view frame,
            const CallOptions &options) override;
  [[nodiscard]] Task<std::expected<std::string, RpcError>>
  RoundTripAsync(const Endpoint &endpoint, std::string frame,
                 CallOptions options) override;

private:
  [[nodiscard]] std::expected<int, RpcError>
  ConnectTo(const Endpoint &endpoint, const CallOptions &options) const;
  [[nodiscard]] Task<std::expected<int, RpcError>>
  ConnectToAsync(const Endpoint &endpoint, const CallOptions &options);
  [[nodiscard]] static std::expected<void, RpcError>
  SendAll(int socket_fd, std::string_view data);
  [[nodiscard]] Task<std::expected<void, RpcError>>
  SendAllAsync(int socket_fd, std::string_view data, int timeout_ms);
  [[nodiscard]] static std::expected<void, RpcError>
  RecvAll(int socket_fd, void *buffer, std::size_t size);
  [[nodiscard]] Task<std::expected<void, RpcError>>
  RecvAllAsync(int socket_fd, void *buffer, std::size_t size, int timeout_ms);
  [[nodiscard]] std::expected<std::string, RpcError>
  ReceiveFrame(int socket_fd) const;
  [[nodiscard]] Task<std::expected<std::string, RpcError>>
  ReceiveFrameAsync(int socket_fd, int timeout_ms);
};

class ClientTransportFactory {
public:
  [[nodiscard]] static std::shared_ptr<ClientTransport> Create();
};

} // namespace hxrpc

#endif
