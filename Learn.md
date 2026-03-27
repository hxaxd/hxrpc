# hxrpc 源码学习指南

这份文档不是 API 手册, 而是怎么读这个仓库的路线图

如果你刚开始看这个项目, 最容易迷路的地方通常不是某个函数, 而是:

- 不知道先看哪一层
- 不知道哪些文件是骨架, 哪些文件是细节
- 不知道一次 RPC 请求到底经过了哪些类

这个仓库其实很适合学习, 因为它分层比较清楚你可以把它先看成一句话:

`RpcClient` 负责把一次调用编码后发出去, `RpcServer` 负责把请求收进来, 分发到 protobuf service, 再把响应发回去

---

## 1. 先建立全局地图

仓库里最值得优先关注的是这几块:

- `src/include/`: 核心抽象和对外接口
- `src/*.cc`: 核心实现
- `benchmark/`: 最小可运行示例, 最适合入门
- `tests/`: 验证每一层行为, 也能反向帮助理解设计

可以先把项目粗略分成 7 层:

1. 配置与启动
2. 服务发现
3. 序列化
4. 协议编解码
5. 网络传输
6. 服务端分发
7. 协程与对象池这些增强能力

---

## 2. 最推荐的阅读顺序

按下面顺序看, 理解会最顺:

1. `README.md`
2. `benchmark/server/server.cc`
3. `benchmark/client/client.cc`
4. `src/include/types.h`
5. `src/include/settings.h` + `src/settings.cc`
6. `src/include/codec.h` + `src/codec.cc`
7. `src/include/serializer.h` + `src/serializer.cc`
8. `src/include/service_registry.h` + `src/service_registry.cc`
9. `src/include/rpc_dispatcher.h` + `src/rpc_dispatcher.cc`
10. `src/include/reactor.h` + `src/reactor.cc`
11. `src/include/connection_manager.h` + `src/connection_manager.cc`
12. `src/include/client_transport.h` + `src/client_transport.cc`
13. `src/include/service_discovery.h` + `src/service_discovery.cc`
14. `src/include/rpc_server.h` + `src/rpc_server.cc`
15. `src/include/rpc_client.h` + `src/rpc_client.cc`
16. `src/include/task.h`
17. `src/include/async_runtime.h` + `src/async_runtime.cc`
18. `src/include/message_pool.h` + `src/message_pool.cc`
19. `tests/`

原因很简单:

- 先看 `benchmark`, 知道用户怎么用
- 再看 `types/settings`, 知道系统里流动的是什么数据
- 再看 `codec/serializer`, 知道数据怎么上网
- 再看 `dispatcher/transport/network`, 知道请求怎么走
- 最后看 `coroutine/message_pool/tests`, 理解增强设计

---

## 3. 第一轮阅读: 只抓主线, 不抠细节

第一轮别试图全记住你只要回答下面 5 个问题就够了:

1. client 从哪里拿到服务地址
2. request 是怎么编码成 frame 的
3. server 是怎么把 TCP 字节流切成完整请求的
4. server 是怎么找到对应 service/method 的
5. response 是怎么再回到 client 的

如果这 5 个问题能答出来, 你就已经看懂这个项目 70% 了

---

## 4. 从可运行示例入手

### 服务端怎么启动

看 `benchmark/server/server.cc`

这里最重要的几行逻辑是:

- 读取配置
- 构造 `hxrpc::RpcServer`
- `RegisterService(new UserService())`
- `server.Run()`

这说明服务端对外暴露的核心接口非常少你不用先懂 epoll, 也能先明白框架是怎么被使用的

### 客户端怎么调用

看 `benchmark/client/client.cc`

这里值得注意的点:

- 先构造 `hxrpc::RpcClient`
- 通过 protobuf descriptor 找到方法
- 构造 request/response
- 调用 `client.Invoke(...)`

这一步会让你知道: 这个项目不是自己发明 service 描述语言, 而是直接依赖 protobuf 已有的 `Service` / `MethodDescriptor` 体系

---

## 5. 先看最基础的数据结构

看 `src/include/types.h`

这是整个项目里非常关键的一份文件, 因为它定义了框架内部传什么:

- `RpcError`: 统一错误
- `CallOptions`: 调用选项, 比如超时, metadata
- `Endpoint`: 地址
- `ServiceInstance`: 服务实例
- `RpcRequest`: 框架层请求
- `RpcResponse`: 框架层响应

一个很重要的区分:

- `RpcRequest/RpcResponse` 不是业务 protobuf 消息
- 它们是框架内部的传输层对象

也就是说, 这个项目把业务对象和框架协议对象明确分开了

---

## 6. 配置层怎么把 yaml 变成强类型

建议一起看:

- `src/include/config.h`
- `src/config.cc`
- `src/include/settings.h`
- `src/settings.cc`
- `src/include/application.h`
- `src/application.cc`

理解顺序:

### 第一步: `hxrpcconfig`

`hxrpcconfig` 是一个很轻量的配置解析器它做的事情不复杂:

- 读配置文件
- 按缩进把 yaml 风格的 key 展平成 `a.b.c`
- 存到 `unordered_map<string, string>`

所以它本质上是文本配置加载器, 不是最终给 RPC 框架直接用的配置对象

### 第二步: `SettingsLoader`

`SettingsLoader` 才负责把字符串配置装配成真正的:

- `ServerConfig`
- `ClientConfig`
- `DiscoveryConfig`

这一步很值得学, 因为它体现了一个很常见的工程模式:

- 底层配置格式可以很松
- 业务层拿到的一定是强类型对象

### 第三步: `hxrpcApplication`

`hxrpcApplication::Init()` 做的事情主要是:

- 解析 `-i <config-file>`
- 加载配置
- 配置日志

所以它更像启动期装配器

---

## 7. 先看协议, 再看网络

很多人读网络框架时会先钻 `epoll`, 其实这个项目更适合先看协议

建议先看:

- `src/include/codec.h`
- `src/codec.cc`
- `src/header.proto`

### 这个项目的帧格式

协议格式是:

```text
----------------------+----------------------+----------------------+------------------+
| 4B total_length      | 4B header_length     | protobuf header      | payload          |
+----------------------+----------------------+----------------------+------------------+
```

你可以把它理解成两层:

- 外层: 长度帧, 解决 TCP 半包/粘包
- 内层: protobuf header + payload, 解决 RPC 语义

### `RpcCodec` 做什么

`RpcCodec` 只关心框架协议:

- 请求怎么编码
- 响应怎么编码
- frame 怎么拆成 header 和 payload
- 长度是否合法

它不关心业务 protobuf 消息长什么样, 这一点非常重要

### 一个容易记住的分工

- `RpcCodec`: 负责线上协议长什么样
- `Serializer`: 负责业务对象怎么变成 payload

这两个层次分开, 是这个项目最值得学习的设计点之一

---

## 8. `Serializer` 是业务对象和 payload 的桥

看:

- `src/include/serializer.h`
- `src/serializer.cc`

当前实现只有 `ProtobufSerializer`, 逻辑不复杂:

- `Serialize` 调 `message.SerializeToString`
- `Deserialize` 调 `ParseFromArray`

但这个抽象的意义在于, 它给未来替换序列化方式留了口子

读这里时你要记住一句话:

`codec` 不碰业务对象, `serializer` 不碰帧结构

---

## 9. 服务端是怎么找到要调用的业务方法的

看:

- `src/include/service_registry.h`
- `src/service_registry.cc`

`ServiceRegistry` 做的事情非常直接:

- 遍历 protobuf service 的所有方法
- 建立 `service_name.method_name -> RegisteredMethod` 映射

所以服务端收到请求后, 并不是靠一堆 if/else 分发, 而是靠注册表查表

这里你要注意 protobuf 自带的运行时能力:

- `Service`
- `Descriptor`
- `MethodDescriptor`
- `CallMethod`

这个项目是把 protobuf 的反射能力真正用起来了

---

## 10. `RpcDispatcher` 是服务端最核心的业务桥接层

看:

- `src/include/rpc_dispatcher.h`
- `src/rpc_dispatcher.cc`

如果你只精读一个文件, 我最推荐这个

`RpcDispatcher::HandleFrame()` 基本代表了服务端处理一次请求的完整流程:

1. `RpcCodec::DecodeRequest`
2. `registry_.Find(service_name, method_name)`
3. 从 `MessagePool` 拿 request/response message 对象
4. `serializer_->Deserialize(...)`
5. 构造 `hxrpccontroller`
6. `service->CallMethod(...)`
7. `serializer_->Serialize(response_message, ...)`
8. `RpcCodec::EncodeResponse(...)`

这个文件把几乎所有模块串起来了

### 这里有一个很关键的限制

`dispatcher` 里明确要求服务端 protobuf 方法必须同步调用 `done->Run()`

也就是说当前版本:

- 支持 protobuf service 风格
- 但不支持异步保存 `done` 后再回调

这就是 README 里说的教学版刻意保持简单的一个具体体现

---

## 11. 服务端网络层怎么分层

建议按这个顺序看:

1. `src/include/reactor.h` + `src/reactor.cc`
2. `src/include/connection_manager.h` + `src/connection_manager.cc`
3. `src/include/rpc_server.h` + `src/rpc_server.cc`

### `Reactor`

`Reactor` 是最底层事件分发器

它只负责:

- `epoll_ctl` 管理 fd
- `epoll_wait` 等待事件
- 可读/可写/错误时调对应回调

它不知道:

- 连接状态
- RPC 协议
- protobuf

所以它是很纯粹的 IO 多路复用封装

### `ConnectionManager`

`ConnectionManager` 在 `Reactor` 之上, 开始理解连接和字节流:

- 监听与 accept
- 每个连接维护输入输出缓冲
- 读 socket
- 处理半包/粘包
- 写 socket
- 关闭连接

最值得仔细看的函数是:

- `Listen`
- `AcceptLoop`
- `HandleReadable`
- `ProcessInput`
- `HandleWritable`

你会看到它把 TCP 流切成一个个完整 frame, 然后通过 `frame_handler_` 交给更上层

### `RpcServer`

`RpcServer` 是服务端门面, 做装配工作:

- 持有 `Reactor`
- 持有 `ConnectionManager`
- 持有 `ServiceRegistry`
- 持有 `Serializer`
- 持有 `MessagePool`
- 持有 `RpcDispatcher`

它自己的逻辑并不复杂, 但特别适合理解框架对象关系

建议你重点看:

- 构造函数里怎么把模块接起来
- `RegisterService`
- `RegisterEndpoints`
- `OnFrame`
- `Run`

你会发现它像一个总控, 不处理细节, 但把所有组件接成一条链

---

## 12. 客户端主线怎么读

建议按这个顺序看:

1. `src/include/service_discovery.h` + `src/service_discovery.cc`
2. `src/include/client_transport.h` + `src/client_transport.cc`
3. `src/include/rpc_client.h` + `src/rpc_client.cc`

### `ServiceResolver`

这是服务发现抽象

当前有两种实现:

- `StaticResolver`
- `ZkResolver`

学习时先只看 `StaticResolver`, 因为最简单等主线通了再看 `ZkResolver` 的 watcher, 本地缓存和刷新逻辑

### `ClientTransport`

这是客户端传输层, 负责:

- connect
- send
- recv
- 收完整 response frame

先看同步版:

- `RoundTrip`
- `ConnectTo`
- `SendAll`
- `ReceiveFrame`

再看异步版:

- `ConnectToAsync`
- `SendAllAsync`
- `RecvAllAsync`
- `ReceiveFrameAsync`

不过这里有个现实细节要注意:

`RoundTripAsync()` 现在直接 `co_return RoundTrip(...)`, 也就是异步接口已经设计好了, 但当前默认路径还是复用同步传输逻辑

这也是很典型的先把接口边界搭好, 再逐步演进实现

### `RpcClient`

`RpcClient::InvokeAsync()` 是客户端最值得精读的函数

它的流程几乎就是客户端版总链路:

1. 从 method descriptor 拿到 service/method 名字
2. `resolver_->Resolve(...)`
3. `serializer_->Serialize(request, ...)`
4. 组装 `RpcRequest`
5. `RpcCodec::EncodeRequest(...)`
6. `transport_->RoundTripAsync(...)`
7. `RpcCodec::DecodeResponse(...)`
8. `serializer_->Deserialize(...)`
9. 把结果拷回用户传入的 response

所以看懂这个函数, 你就看懂了客户端

---

## 13. 一次 RPC 请求到底怎么流动

这是最值得你自己复述一遍的主线

### 客户端发送

在 `RpcClient::InvokeAsync()` 里:

- 先找服务实例
- 再把业务 request 序列化成 payload
- 再编码成 frame
- 再通过 `ClientTransport` 发到服务端

### 服务端接收

在 `ConnectionManager` 里:

- socket 收到字节流
- 累积到 `input_buffer`
- `ProcessInput()` 按长度前缀切 frame
- 完整 frame 交给 `RpcServer::OnFrame()`

### 服务端分发

在 `RpcServer::OnFrame()` 里:

- 调 `dispatcher_.HandleFrame(frame)`

在 `RpcDispatcher::HandleFrame()` 里:

- 解 request
- 查 service/method
- 反序列化业务请求
- 调用 protobuf service
- 序列化业务响应
- 编码 response frame

### 客户端接收响应

在 `RpcClient::InvokeAsync()` 里:

- 收到 response frame
- 解码成 `RpcResponse`
- 检查状态码
- 反序列化 payload 到业务 response

如果你愿意, 可以把这条链写成一句话背下来:

`descriptor -> resolve -> serialize -> encode -> transport -> decode -> dispatch -> service -> encode -> transport -> decode -> deserialize`

---

## 14. 协程相关代码应该怎么看

先看:

- `src/include/task.h`
- `src/include/async_runtime.h`
- `src/async_runtime.cc`

### `Task<T>`

`Task<T>` 是这个项目自己的协程返回类型

你重点理解三件事:

- `promise_type`
- `Get()` 会阻塞等待协程完成
- `operator co_await()` 允许协程之间继续串联

它不是为了做复杂调度器, 而是先把能 co_await, 能拿结果的最小模型搭出来

### `AsyncRuntime`

`AsyncRuntime` 是一个独立的小型等待器:

- 用自己的 epoll 线程等待 fd 就绪
- 用 `eventfd` 唤醒循环线程
- 超时后恢复协程

它的目标不是替代服务端的 `Reactor`, 而是给客户端异步 IO 提供一个 await 能力

读这里时要注意一个设计点:

- 服务端事件循环是 `Reactor`
- 客户端协程等待器是 `AsyncRuntime`

两个名字都和 epoll 有关, 但职责并不一样

---

## 15. 对象池为什么存在

看:

- `src/include/message_pool.h`
- `src/message_pool.cc`

`MessagePool` 只做一件事:

- 复用 protobuf message 对象

它不是一个通用对象池, 而是非常聚焦地解决 protobuf message 频繁创建销毁的问题

你看 `RpcDispatcher` 和 `RpcClient` 时会发现:

- 反序列化前先 `Acquire`
- 用完后靠 `PooledMessage` 的析构自动归还

这是一种很实用的小型 RAII 设计

---

## 16. 服务发现第二轮再看

主线看通以后, 再回来看:

- `src/include/service_discovery.h`
- `src/service_discovery.cc`
- `src/include/zookeeperutil.h`
- `src/zookeeperutil.cc`

第二轮建议重点看这些问题:

1. 静态发现如何用 `service.method` 做 key
2. Zookeeper 路径如何组织
3. watcher 触发后本地缓存怎么刷新
4. 为什么 resolver 和 registrar 要分开抽象

这里体现的是框架演进能力, 而不是最基本调用链

---

## 17. 测试怎么反过来帮你读源码

这个仓库的测试很适合拿来做阅读导航

推荐顺序:

1. `tests/codec_test.cc`
2. `tests/config_test.cc`
3. `tests/dispatcher_test.cc`
4. `tests/end_to_end_test.cc`
5. `tests/connection_boundary_test.cc`
6. `tests/metadata_test.cc`
7. `tests/message_pool_test.cc`
8. `tests/coroutine_client_test.cc`
9. `tests/failure_test.cc`
10. `tests/discovery_test.cc`

阅读方法:

- 先看测试想验证什么
- 再倒推哪些模块参与了这个行为

例如:

- `codec_test` 帮你理解协议字段
- `end_to_end_test` 帮你理解完整链路
- `connection_boundary_test` 帮你理解半包/粘包处理
- `metadata_test` 帮你理解 metadata 如何透传到 controller
- `coroutine_client_test` 帮你理解异步接口怎么用

如果你容易在实现细节里迷路, 就多从测试反推

---

## 18. 看源码时最值得盯住的几个设计点

### 设计点 1: 层次分得很清楚

- `codec` 只管协议
- `serializer` 只管业务对象序列化
- `transport` 只管收发
- `dispatcher` 只管服务分发
- `server/client` 只管装配主链路

这是整个项目最值得学习的地方

### 设计点 2: 大量使用 protobuf 反射能力

不是把 protobuf 当纯结构体生成器, 而是用到了:

- `Service`
- `MethodDescriptor`
- `CallMethod`
- request/response prototype

这让框架层真正拥有按方法名动态分发的能力

### 设计点 3: 先把边界留好, 再逐步增强

比如:

- `Serializer` 已经可替换
- `ServiceResolver` 已经可替换
- `ClientTransport` 已经有同步/异步接口
- `MessagePool` 已经插进主链路

这说明作者在刻意给后续演进留位置

### 设计点 4: 教学项目故意保持简单

比如当前限制包括:

- 客户端一请求一连接
- 服务端 protobuf 回调必须同步 `done->Run()`
- 没有连接池, 重试, 熔断, 限流, metrics, trace

这些不是没想到, 而是先不做, 这样你更容易看懂骨架

---

## 19. 我建议你做的三轮学习法

### 第一轮: 只看调用链

目标:

- 知道一个请求从 client 到 server 再回来经过哪些类

只看这些文件就够:

- `benchmark/server/server.cc`
- `benchmark/client/client.cc`
- `rpc_client`
- `client_transport`
- `codec`
- `rpc_server`
- `connection_manager`
- `rpc_dispatcher`

### 第二轮: 看抽象边界

目标:

- 知道为什么要拆成这些模块

重点看:

- `types`
- `settings`
- `serializer`
- `service_registry`
- `service_discovery`

### 第三轮: 看增强能力

目标:

- 理解协程, 对象池, 动态发现是怎么接进骨架的

重点看:

- `task`
- `async_runtime`
- `message_pool`
- `service_discovery`
- `zookeeperutil`

---

## 20. 如果你想边读边做实验

下面这些实验特别适合入门:

1. 在 `RpcClient::InvokeAsync()` 打日志, 观察请求链路
2. 在 `RpcDispatcher::HandleFrame()` 打日志, 观察服务端分发
3. 改 `codec` 的 header 字段, 看看需要同步改哪些地方
4. 给 `CallOptions.metadata` 填不同值, 观察服务端 controller 是否收到
5. 跑 `connection_boundary_test`, 理解半包/粘包为什么必须有长度帧
6. 跑 `coroutine_client_test`, 观察协程接口和同步接口的关系

这样你读代码不会太抽象

---

## 21. 一些容易卡住的地方

### 为什么既有 `RpcRequest`, 又有 protobuf request

因为这俩不是一层东西:

- `RpcRequest` 是框架传输对象
- protobuf request 是业务对象

### 为什么 `RpcServer` 看起来代码不多

因为它是门面和装配层, 不承担具体网络读写和业务分发细节

### 为什么异步客户端里很多函数写了, 但主流程还是像同步

因为这个项目明显在逐步演进协程支持, 接口已经搭好, 部分实现还保留简化路径

### 为什么测试里大量直接 `assert`

因为这是偏教学/工程练习型仓库, 测试更追求直接表达行为, 而不是测试框架的复杂封装

---

## 22. 最后给你的最短学习路线

如果你时间不多, 只看下面这些文件:

1. `benchmark/server/server.cc`
2. `benchmark/client/client.cc`
3. `src/include/types.h`
4. `src/codec.cc`
5. `src/rpc_client.cc`
6. `src/connection_manager.cc`
7. `src/rpc_dispatcher.cc`
8. `src/rpc_server.cc`
9. `tests/end_to_end_test.cc`

看完这 9 个文件, 你基本就能把整个项目讲明白

---

## 23. 你可以怎么继续问我

如果你愿意, 我下一步还能继续帮你做这几种版本:

- 按服务端视角单独写一版学习笔记
- 按客户端视角单独写一版学习笔记
- 画一张单次 RPC 时序图文字版
- 把 `benchmark/client/client.cc` 逐行讲解
- 把 `RpcDispatcher::HandleFrame()` 逐行讲解

如果只选一个, 我最推荐下一步讲 `RpcDispatcher::HandleFrame()`, 因为它最像整个框架的十字路口
