#ifndef HXRPC_ZOOKEEPER_UTIL_H
#define HXRPC_ZOOKEEPER_UTIL_H

#define THREADED

#include <zookeeper/zookeeper.h>

#include <expected>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "types.h"

class ZkClient {
 public:
  // 子节点监听上下文: 保存监听路径与回调, 确保 watcher 生命周期可控
  struct ChildWatch;

  ZkClient();
  ~ZkClient();

  ZkClient(const ZkClient&) = delete;
  ZkClient& operator=(const ZkClient&) = delete;

  // 启动连接: 无参版本从全局配置读取 ZooKeeper 地址
  [[nodiscard]] std::expected<void, hxrpc::RpcError> Start();
  // endpoint: ZooKeeper 服务地址
  // 返回: 成功表示会话已建立失败为发现层错误
  [[nodiscard]] std::expected<void, hxrpc::RpcError> Start(
      const hxrpc::Endpoint& endpoint);
  // 创建节点若节点已存在则视为成功 (幂等行为)
  [[nodiscard]] std::expected<void, hxrpc::RpcError> CreateNode(
      const char* path, const char* data, int datalen, int flags = 0);
  // 递归确保路径存在 (逐层创建持久节点)
  [[nodiscard]] std::expected<void, hxrpc::RpcError> EnsurePath(
      const char* path);
  // 读取节点数据或子节点列表
  [[nodiscard]] std::expected<std::string, hxrpc::RpcError> GetData(
      const char* path) const;
  [[nodiscard]] std::expected<std::vector<std::string>, hxrpc::RpcError>
  GetChildren(const char* path) const;
  // 读取子节点并注册 watcheron_change 在子节点变化时触发
  [[nodiscard]] std::expected<std::vector<std::string>, hxrpc::RpcError>
  GetChildrenWatched(const std::string& path,
                     std::function<void(const std::string&)> on_change);

 private:
  zhandle_t* handle_{nullptr};
  // 保存 watch 上下文, 防止回调期间上下文提前析构
  std::vector<std::shared_ptr<ChildWatch>> child_watches_;
};

#endif
