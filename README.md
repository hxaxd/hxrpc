# hxrpc

一个基于 C++20 协程的轻量级 RPC 框架。

## 目前的实现

* **网络 I/O**: 底层依然是标准的 epoll。在 Reactor 模型基础上，把 socket 的可读/可写事件挂载到协程的 `promise_type` 上，规避了线程池模型的线程切换开销。
* **粘包处理**: 简单的自定义 TLV 格式。应用层维护了一个 RingBuffer，按 Length 字段切包，处理 TCP 字节流常见的半包和粘包问题。
* **序列化与内存分配**: 序列化用的 Protobuf。考虑到 RPC 调用非常频繁，为了减少堆内存分配带来的系统调用开销和内存碎片，给 Protobuf 的 Message 对象加了一个简单的对象池（Object Pool）来做循环复用。
* **服务注册发现**: 强依赖 Zookeeper，靠 ZK 的 watcher 机制做基础的服务节点注册和宕机剔除。

## 目前的缺陷

1. **内核数据拷贝开销**: 目前的读写还是用的标准 `read/write` 系统调用。数据从网卡到内核协议栈，再拷贝到用户态 buffer，最后 protobuf 反序列化，中间的多次内存拷贝开销完全没省掉。
2. **服务治理太糙**: 路由只写了最基础的随机和轮询。没有平滑负载均衡（比如 P2C），也没有做动态熔断和限流。一旦遇到突发流量打满，节点基本上只能等死。
3. **定时器性能差**: 目前 RPC 的超时重试机制，用的是很基础的定时器逻辑。如果高并发下出现大面积超时，遍历和清理定时任务的开销会把 CPU 吃满。
4. **无观测性**: 没打点，也没接入 trace 系统，如果跑在分布式环境里出了超时或者丢包，链路根本没法查。

## 后续演进

如果要继续把这个框架往实用级方向推，下一步重点要在 I/O 栈和高性能机制上动刀：

* **引入 RDMA (Kernel Bypass)**: 传统的 TCP/IP 协议栈延迟太高，打算在传输层做个抽象接口，除了现在的 TCP Socket，增加对 RDMA（通过 `libibverbs`）的支持。直接绕过操作系统内核实现网卡到内存的读写，把内部 RPC 通信延迟压到微秒级。
* **替换底层 I/O 引擎**: 考虑逐步把底层的 epoll 替换成 `io_uring`，进一步压榨系统调用的性能；同时在特定场景看能不能引入 `sendfile` 做零拷贝。
* **重构定时器（时间轮算法）**: 把现在的超时控制模块重构成层级时间轮（Hierarchical Timing Wheels），把海量连接的心跳保活和超时任务的增删时间复杂度降到 O(1)。

## 编译与运行

### 环境

* **OS**: Linux
* **Compiler**: GCC 11+ / Clang 14+ (必须支持 C++ 23 编译标准)
* **Dependencies**: CMake (3.20+), Protobuf, Zookeeper C Client, muduo
* 请自行安装 Zookeeper 服务。
* 请自行编译 `muduo` 库。

### 编译

```bash
git clone https://github.com/hxaxd/hxrpc.git
cd hxrpc
mkdir build && cd build
cmake ..
make -j$(nproc)
```

编译成功后，可执行文件将生成在项目的 `bin/` 目录下。

### Benchmark

* 我们在 `2C2G` 硬件规格下开展了极限基准压测
    * 可稳定支撑 **1w+ 活跃并发连接**。
    * 较低并发数下, 单机 QPS 达到 **2.5万+**，且 P99 延迟低于 12ms。

```bash
# 确保在项目根目录下
chmod +x benchmark.sh

# 修改 test.conf 中的压测参数

./benchmark.sh
```

#### 性能测试方法

空载压测方案：

1. **服务端**：
   服务端接收到 `Login` 请求后，不执行任何数据库操作或磁盘 I/O，而是直接在内存中构建 Protobuf 响应并立即 `done->Run()` 返回。
2. **客户端复用数据包**：
   压测客户端在循环外预先完成请求的 Protobuf 序列化，并构建好带有包头长度的物理字节流。在压测中，通过 `send` 直接拷贝内存。
3. **无锁统计**：
   开启 `TCP_NODELAY`。统计 QPS 和延迟时，采用 Thread-Local 机制在各线程内独立计数，最终在主线程统一汇集。
