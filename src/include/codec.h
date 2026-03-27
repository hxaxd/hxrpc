#ifndef HXRPC_CODEC_H
#define HXRPC_CODEC_H

#include <expected>
#include <string>
#include <string_view>

#include "types.h"

namespace hxrpc {

// RpcCodec 定义了框架统一的线协议:
// 1) 帧级别: 长度前缀 + header + payload；
// 2) 语义级别: 请求/响应头字段与业务负载的拼装与校验
// 编解码模块把线上的字节流格式集中到一起, 避免协议细节散落在
// channel/provider 中对教学项目来说, 这样更容易讲清楚数据在不同层之间
// 如何流动, 也能让后续扩展字段时只改一个地方
class RpcCodec {
 public:
  // 将 RPC 请求编码为可直接发送的网络帧
  // 返回值: 成功时为完整帧字节串；失败时返回序列化或参数错误
  [[nodiscard]] static std::expected<std::string, RpcError> EncodeRequest(
      const RpcRequest& request);

  // 将 RPC 响应编码为可直接发送的网络帧
  // 返回值: 成功时为完整帧字节串；失败时返回序列化错误
  [[nodiscard]] static std::expected<std::string, RpcError> EncodeResponse(
      const RpcResponse& response);

  // 从网络帧中解码出 RPC 请求
  // 参数 frame 必须是完整单帧数据 (含长度前缀) ；
  // 返回值: 成功时得到结构化请求, 失败时给出包格式/字段校验错误
  [[nodiscard]] static std::expected<RpcRequest, RpcError> DecodeRequest(
      std::string_view frame);

  // 从网络帧中解码出 RPC 响应
  // 参数 frame 必须是完整单帧数据 (含长度前缀) ；
  // 返回值: 成功时得到结构化响应, 失败时给出包格式/字段校验错误
  [[nodiscard]] static std::expected<RpcResponse, RpcError> DecodeResponse(
      std::string_view frame);
};

}  // namespace hxrpc

#endif
