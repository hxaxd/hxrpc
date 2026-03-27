#ifndef HXRPC_CLIENT_TRANSPORT_H
#define HXRPC_CLIENT_TRANSPORT_H

#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "task.h"
#include "types.h"

namespace hxrpc {

// 客户端传输抽象层: 输入已编码请求帧, 输出已编码响应帧
// 该层只关心帧级别 I/O, 不参与序列化与编解码策略选择
class ClientTransport {
 public:
  virtual ~ClientTransport() = default;
  // 同步单次往返
  // endpoint: 目标实例地址；frame: 已带长度前缀的完整请求帧；options:
  // 超时等调用参数 返回: 成功为完整响应帧, 失败为
  // RpcError (网络错误, 超时, 无效包等)
  [[nodiscard]] virtual std::expected<std::string, RpcError> RoundTrip(
      const Endpoint& endpoint, std::string_view frame,
      const CallOptions& options) = 0;
  // 异步单次往返, 语义与 RoundTrip 保持一致
  [[nodiscard]] virtual Task<std::expected<std::string, RpcError>>
  RoundTripAsync(const Endpoint& endpoint, std::string frame,
                 CallOptions options) = 0;
};

// TCP 传输实现: 提供连接复用, 收发完整性保证与基础超时控制
class TcpClientTransport final : public ClientTransport {
 public:
  ~TcpClientTransport() override;
  [[nodiscard]] std::expected<std::string, RpcError> RoundTrip(
      const Endpoint& endpoint, std::string_view frame,
      const CallOptions& options) override;
  [[nodiscard]] Task<std::expected<std::string, RpcError>> RoundTripAsync(
      const Endpoint& endpoint, std::string frame,
      CallOptions options) override;

 private:
  // 与目标地址建立新连接 (同步/异步) , 失败时返回网络类错误
  [[nodiscard]] std::expected<int, RpcError> ConnectTo(
      const Endpoint& endpoint, const CallOptions& options) const;
  [[nodiscard]] Task<std::expected<int, RpcError>> ConnectToAsync(
      const Endpoint& endpoint, const CallOptions& options);
  // 保证发送/接收指定字节数完整完成；中途异常统一映射为 RpcError
  [[nodiscard]] static std::expected<void, RpcError> SendAll(
      int socket_fd, std::string_view data);
  [[nodiscard]] Task<std::expected<void, RpcError>> SendAllAsync(
      int socket_fd, std::string_view data, int timeout_ms);
  [[nodiscard]] static std::expected<void, RpcError> RecvAll(int socket_fd,
                                                             void* buffer,
                                                             std::size_t size);
  [[nodiscard]] Task<std::expected<void, RpcError>> RecvAllAsync(
      int socket_fd, void* buffer, std::size_t size, int timeout_ms);
  // 从连接读取单个响应帧, 并校验长度字段, 防止异常帧触发越界/过量分配
  [[nodiscard]] std::expected<std::string, RpcError> ReceiveFrame(
      int socket_fd) const;
  [[nodiscard]] Task<std::expected<std::string, RpcError>> ReceiveFrameAsync(
      int socket_fd, int timeout_ms);

  // 连接池辅助: 按 endpoint 维度借还连接, 降低重复建连开销
  [[nodiscard]] static std::string EndpointKey(const Endpoint& endpoint);
  [[nodiscard]] std::expected<void, RpcError> ConfigureSocket(
      int socket_fd, const CallOptions& options) const;
  [[nodiscard]] std::expected<int, RpcError> BorrowConnection(
      const Endpoint& endpoint, const CallOptions& options);
  [[nodiscard]] Task<std::expected<int, RpcError>> BorrowConnectionAsync(
      const Endpoint& endpoint, const CallOptions& options);
  void ReturnConnection(const Endpoint& endpoint, int socket_fd);
  static void CloseSocket(int socket_fd);

  std::mutex pool_mutex_;
  // endpoint -> 空闲 socket 列表
  std::unordered_map<std::string, std::vector<int>> idle_pool_;
  // 每个 endpoint 最多缓存的空闲连接数, 避免长时间空闲 fd 无上限增长
  static constexpr std::size_t kMaxIdlePerEndpoint = 8;
};

class ClientTransportFactory {
 public:
  [[nodiscard]] static std::shared_ptr<ClientTransport> Create();
};

}  // namespace hxrpc

#endif
