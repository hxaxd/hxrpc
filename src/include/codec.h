#ifndef HXRPC_CODEC_H
#define HXRPC_CODEC_H

#include "types.h"
#include <expected>
#include <string>
#include <string_view>

namespace hxrpc {

// 编解码模块把“线上的字节流格式”集中到一起，避免协议细节散落在
// channel/provider 中。对教学项目来说，这样更容易讲清楚数据在不同层之间
// 如何流动，也能让后续扩展字段时只改一个地方。
class RpcCodec {
public:
  [[nodiscard]] static std::expected<std::string, RpcError>
  EncodeRequest(const RpcRequest &request);

  [[nodiscard]] static std::expected<std::string, RpcError>
  EncodeResponse(const RpcResponse &response);

  [[nodiscard]] static std::expected<RpcRequest, RpcError>
  DecodeRequest(std::string_view frame);

  [[nodiscard]] static std::expected<RpcResponse, RpcError>
  DecodeResponse(std::string_view frame);
};

} // namespace hxrpc

#endif
