#ifndef HXRPC_SERVICE_REGISTRY_H
#define HXRPC_SERVICE_REGISTRY_H

#include <google/protobuf/service.h>

#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "types.h"

namespace hxrpc {

// RegisteredMethod 描述一次可调用 RPC 方法的静态绑定信息
struct RegisteredMethod {
  // protobuf service 名称 (descriptor->name())
  std::string service_name;
  // protobuf method 名称 (method->name())
  std::string method_name;
  // 业务服务对象实例 (不拥有生命周期)
  google::protobuf::Service* service{nullptr};
  // 方法描述符 (来自 protobuf 反射, 不拥有生命周期)
  const google::protobuf::MethodDescriptor* method{nullptr};
};

// ServiceRegistry 保存服务名 + 方法名 -> protobuf service/method的映射
// Dispatcher 只依赖这个注册表, 不直接扫描业务类
class ServiceRegistry {
 public:
  // 注册一个 protobuf Service 的全部方法
  // 若同名键重复, 后注册项会覆盖先前映射
  void RegisterService(google::protobuf::Service* service);

  // 按服务名与方法名查找目标方法
  // 返回: 成功为 RegisteredMethod；失败区分 unknown service / unknown method
  [[nodiscard]] std::expected<RegisteredMethod, RpcError> Find(
      std::string_view service_name, std::string_view method_name) const;

  // 列出当前所有已注册方法, 供服务发现或诊断使用
  [[nodiscard]] std::vector<RegisteredMethod> ListMethods() const;

 private:
  std::unordered_map<std::string, RegisteredMethod> methods_;
};

}  // namespace hxrpc

#endif
