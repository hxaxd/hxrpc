#ifndef HXRPC_CONNECTION_MANAGER_H
#define HXRPC_CONNECTION_MANAGER_H

#include <functional>
#include <string>
#include <unordered_map>

#include "reactor.h"
#include "types.h"

namespace hxrpc {

// ConnectionManager 构建在 Reactor 之上, 负责连接生命周期和字节流缓冲
// 它把 TCP 字节流整理成完整帧, 再把完整帧交给上层 dispatcher
// 该层不理解具体业务方法, 只处理连接 + 帧边界 + 收发状态机
class ConnectionManager {
 public:
  explicit ConnectionManager(Reactor& reactor);
  ~ConnectionManager();

  // 在给定 endpoint 上创建监听 socket, 并把监听 fd 注册到 Reactor
  // 返回值: true 表示监听建立成功；false 表示任一底层步骤失败
  [[nodiscard]] bool Listen(const Endpoint& endpoint);
  // 设置完整帧处理器
  // 参数: connection_fd 为来源连接, frame 为完整协议帧 (二进制)
  void SetFrameHandler(std::function<void(int, std::string)> frame_handler);
  // 异步发送: 把 frame 追加到连接输出缓冲, 并触发可写处理
  void Send(int connection_fd, std::string frame);
  // 主动关闭连接或监听 fd, 并清理对应状态
  void Close(int connection_fd);

 private:
  // 单帧长度上限 (保护内存, 避免异常长度导致资源耗尽)
  static constexpr std::uint32_t kMaxFrameBodySize = 4 * 1024 * 1024;

  // 单连接的读写状态
  struct ConnectionState {
    // 累积接收缓冲 (可能包含半包或多包)
    std::string input_buffer;
    // 待发送缓冲 (可能包含多个响应帧拼接)
    std::string output_buffer;
    // output_buffer 已成功发送的字节数
    std::size_t bytes_sent{0};
  };

  // 把 fd 设为非阻塞模式；Reactor 假设所有 socket 都是非阻塞
  static bool SetNonBlocking(int fd);
  // 消费监听 fd 上的可读事件, 批量 accept 新连接直到 EAGAIN
  void AcceptLoop(int listen_fd);
  // 消费连接可读事件并把字节流灌入 input_buffer
  void HandleReadable(int connection_fd);
  // 消费连接可写事件, 尽力刷出 output_buffer
  void HandleWritable(int connection_fd);
  // 从 input_buffer 中按长度字段切分完整帧并上抛给 frame_handler_
  void ProcessInput(int connection_fd);
  // 根据是否有待发送数据更新 epoll 关注事件 (仅读/读写)
  void UpdateInterest(int connection_fd, bool want_write);

  Reactor& reactor_;
  int listen_fd_{-1};
  std::unordered_map<int, ConnectionState> connections_;
  std::function<void(int, std::string)> frame_handler_;
};

}  // namespace hxrpc

#endif
