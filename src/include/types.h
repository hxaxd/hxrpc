#ifndef HXRPC_TYPES_H
#define HXRPC_TYPES_H

// src/include/types.h
// 框架核心数据类型定义。
// 覆盖 RPC 状态码、请求/响应模型、服务实例、调用选项等跨模块共享结构。

#include <cstdint>
#include <string>

namespace hxrpc {

enum class RpcStatusCode : std::int32_t {
  // 调用成功。
  kOk = 0,
  // 包格式非法或不完整。
  kInvalidPacket = 1,
  // 服务名未注册。
  kUnknownService = 2,
  // 方法名未注册。
  kUnknownMethod = 3,
  // 序列化失败。
  kSerializeError = 4,
  // 反序列化失败。
  kDeserializeError = 5,
  // 网络层错误（连接、读写、超时等）。
  kNetworkError = 6,
  // 服务发现失败。
  kDiscoveryError = 7,
  // 框架内部错误。
  kInternalError = 8,
};

struct RpcError {
  // 错误码与错误描述文本。
  RpcStatusCode code{RpcStatusCode::kOk};
  std::string message;
};

struct CallOptions {
  // 单次调用超时（毫秒）。
  int timeout_ms{3000};
  // 透传元数据，供链路追踪/鉴权等场景使用。
  std::string metadata;
};

struct Endpoint {
  // 主机地址与端口。
  std::string host;
  std::uint16_t port{0};

  // 转换为 host:port 字符串。
  // 返回：格式化地址字符串。
  [[nodiscard]] std::string ToString() const;

  // 校验地址是否完整。
  // 返回：host 非空且端口非 0 时为 true。
  [[nodiscard]] bool IsValid() const { return !host.empty() && port != 0; }
};

struct ServiceInstance {
  // 实例唯一标识与网络地址。
  std::string id;
  Endpoint endpoint;
};

enum class ResolverSelectionPolicy {
  // 随机选择实例。
  kRandom,
};

struct RpcRequest {
  // 请求标识与路由信息。
  std::uint64_t request_id{0};
  std::string service_name;
  std::string method_name;
  // 附加元数据与序列化后的负载。
  std::string metadata;
  std::string payload;
};

struct RpcResponse {
  // 与请求对应的响应标识与状态。
  std::uint64_t request_id{0};
  RpcStatusCode status{RpcStatusCode::kOk};
  // 错误文本与响应负载。
  std::string error_text;
  std::string payload;

  // 判定响应是否成功。
  // 返回：status == kOk。
  [[nodiscard]] bool Ok() const { return status == RpcStatusCode::kOk; }
};

}  // namespace hxrpc

#endif
