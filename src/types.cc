// src/types.cc
// 通用类型辅助函数实现

#include "types.h"

namespace hxrpc {

std::string Endpoint::ToString() const {
  // 设计原因: 统一地址文本格式, 减少日志, 配置, 服务发现模块的重复拼接逻辑
  return host + ":" + std::to_string(port);
}

}  // namespace hxrpc
