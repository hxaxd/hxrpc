# hxrpc M1.1 调试复盘（为什么没有一次性做成“完整异步版”）

> 日期：2026-03-27  
> 范围：`RoundTripAsync` 真异步链路 + 协程运行时稳定性  
> 结论先行：**不是业务逻辑问题，而是自研 `Task + AsyncRuntime` 的生命周期语义不完整，导致真实异步路径触发 UAF/崩溃。**

---

## 1. 目标定义与“未完成项”边界

### 1.1 计划中的 M1.1（完整态）

- 客户端 `RoundTripAsync` 走真实异步链路：
    - `ConnectToAsync`
    - `SendAllAsync`
    - `ReceiveFrameAsync`
- 连接复用池与异步路径兼容（复用连接可切换阻塞/非阻塞）
- 全量测试 + ASAN 稳定通过

### 1.2 本轮实际达到的状态（稳定起步版）

- 保留了 M1 的连接复用池（已测试验证）
- 修复了 `AsyncRuntime` 中一个核心悬空写入风险（增加取消/状态保护）
- 为保证主干可用，`RoundTripAsync` 回退到稳定封装路径（调用稳定同步 round-trip）
- WSL 下全量测试通过（11/11）

**所以“没完全搞定”的点非常明确：`RoundTripAsync` 的“全链路真实异步”尚未最终合入主干。**

---

## 2. 复现现象（关键症状）

真实异步链路开启后，出现以下高频症状：

1. `end_to_end_test` / `metadata_test` / `coroutine_client_test` 在请求成功后进程崩溃
2. ASAN 报错核心为：
   - `heap-use-after-free`
   - 崩溃点集中在 `AsyncRuntime::ResumeReady(...)`
3. 伴随现象：
   - `free(): invalid pointer`
   - 偶发 `response frame length is invalid`（连接状态/模式切换错配时）

也就是说：**请求链路本身可能成功，但收尾阶段（协程恢复/析构）发生内存安全问题。**

---

## 3. 调试时间线（压缩版）

### 阶段 A：功能合入

- 合入连接复用池（按 endpoint 复用）
- 把 `RoundTripAsync` 切为真实 async pipeline

### 阶段 B：首次失败

- WSL 全测出现 3 个用例段错误
- 初步怀疑：fd 模式、连接复用、协程恢复时机

### 阶段 C：ASAN 定位

- 在 WSL 的 ASAN 构建中，稳定复现 UAF
- 栈顶多次落在 `AsyncRuntime::ResumeReady`
- 释放链常见于 `Task<...>::~Task` / awaiter 析构后的协程帧销毁

### 阶段 D：尝试修复

做过的修复尝试包括：

1. 移除危险写法：`co_return co_await ...`
2. 调整 socket 阻塞模式切换（复用连接在 sync/async 路径间切换）
3. 改造 `AsyncRuntime` 等待状态（减少裸指针悬挂风险）
4. 改 `Task` 所有权模型（共享状态尝试）

### 阶段 E：收敛决策

- 在“完整异步链路 + 当前 Task 语义”组合下仍可触发协程生命周期问题
- 为保证仓库主干稳定，回退 `RoundTripAsync` 到稳定封装路径
- 确认 WSL 全量测试恢复绿色

---

## 4. 根因分析（为什么难在这里）

### 根因 1：`AsyncRuntime` 最初持有 awaiter 裸引用，天然存在悬挂窗口

最初设计中，等待注册对象会持有 awaiter 指针/引用；当协程帧提前销毁或 awaiter 先析构，事件线程继续回调会写入已释放内存。

这是经典竞态：

$$
\text{Coroutine Frame Destroyed} \;\parallel\; \text{Epoll/Timeout Resume}
$$

未建立“可取消 + 可见状态 + 幂等完成”三件套时，UAF 极易发生。

### 根因 2：自研 `Task` 语义过于最小化，不足以覆盖嵌套 await 的复杂生命周期

当前 `Task` 实现偏教学最小集：

- 缺少成熟的取消语义
- 缺少严格的句柄所有权/引用计数模型
- 对“右值 await + 嵌套协程 + 异步恢复线程”组合不够鲁棒

真实异步链路会放大这些问题，因为 `connect/send/recv` 都是挂起点，协程帧生命周期更复杂。

### 根因 3：连接复用 + 阻塞模式切换增加状态空间

复用连接跨 sync/async 两条路径时，fd 阻塞/非阻塞状态必须一致地切换。

一旦状态错位，可能出现：

- 读写行为与预期不一致
- 读半包后进入异常分支
- 误报 frame 长度异常

这不是唯一根因，但会增加复现不稳定性，干扰主因定位。

---

## 5. 为什么我当时选择“稳定起步版”而不是硬上

当时有两个选项：

1. 继续在主干上硬追完整异步（高风险，可能持续破坏测试稳定）
2. 先保住主干可用性，再拆分 runtime 语义修复

我选择了 2，因为工程上更稳：

- 你的仓库目标是可迭代，不是“一次豪赌”
- 主干红了以后，后续迭代会被放大阻塞
- 先拿到 100% 回归通过，后续可以在 feature 分支继续攻坚

---

## 6. 已做并有效的工作

- 新增连接复用测试：`tests/client_transport_reuse_test.cc`
- 连接复用池稳定运行（WSL 全测通过）
- `AsyncRuntime` 增加取消与状态保护改造（降低悬空回调风险）
- 清理一批危险协程写法（`co_return co_await`）

---

## 7. 后续要把 1.1 “完整做成”需要什么

### 7.1 必做改造（优先级 P0）

1. **重构 `Task` 生命周期模型**
   - 明确句柄所有权（谁负责 destroy）
   - 嵌套 await 时保证句柄不会被提前销毁
   - 定义取消传播行为

2. **统一 Await 注册协议**
   - `AsyncRuntime` 仅与“共享等待状态”交互
   - 注册/取消/完成三态原子化，且幂等

3. **真实异步链路分段上线**
   - 先只 async connect
   - 再 async send
   - 最后 async recv
   - 每一步都跑 ASAN + 全测

### 7.2 推荐新增测试（P1）

- 协程压力回归（大量短连接/超时/取消）
- `WaitFor` 超时与取消交错测试
- 连接复用 + async/sync 混合调用测试
- ASAN 常驻 CI job（至少 nightly）

---

## 8. 给评审/团队的简版结论

- 1.1 没一次到位的核心原因：**runtime 生命周期语义短板，不是业务层实现粗心。**
- 现阶段已把主干稳定性恢复到可迭代状态（WSL 全绿）。
- 下一阶段要完成完整异步，必须先补 `Task/AsyncRuntime` 的所有权与取消语义，再逐段放开 async pipeline。
