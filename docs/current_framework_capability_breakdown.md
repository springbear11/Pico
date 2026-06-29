# PicoATE 当前框架功能拆解

日期：2026-06-24

这份文档不是宣传稿，也不是“我们已经很厉害”的总结。它的目的很简单：

把 PicoATE 现在到底有什么、做到什么程度、能跑出什么效果、哪些地方只是雏形、哪些地方容易误解，全部摊开讲清楚。

你现在觉得“有点不太对劲”，这个感觉值得认真对待。因为我们已经连续做了很多底层能力，这时候如果不暂停盘点，很容易继续往前堆功能，但心里反而越来越不踏实。

所以这份文档按“大白话”写。

## 0. 一句话结论

PicoATE 现在已经不是单纯的架构文档了，它已经有一个能编译、能测试、能通过命令行跑 JSON 测试流程的调度内核 MVP。

它目前具备：

- JSON 测试流程加载和编译
- 多 UUT 执行
- Retry
- Cleanup
- Barrier
- Resource
- For Loop
- Runtime 变量替换
- 外部模块调用
- Python 脚本调用
- NativeHost 加载 C/C++ DLL
- 模拟 CAN DLL 示例
- DeviceSessionManager 硬件连接生命周期核心
- Station config JSON
- StationRuntime 和 CLI `--station`
- PersistentQProcessTransport 和 Fake Instrument Host
- 业务模块通过逻辑设备 ID 访问设备 session/proxy
- fake DMM/CAN adapter spike
- 结构化测量值和上下限结果
- CLI 运行和打印结果
- QtTest 自动化测试

但它还不是完整产品。

它现在还没有：

- 真正的 Qt UI
- 可视化 Sequence 编辑器
- 真实工站管理
- 真实硬件接入验证
- 生产级真实硬件长驻 Host
- 崩溃后恢复
- 生产级报表
- 权限、用户、数据库、限制库等产品外围能力

更直白一点：

```text
现在已经做出了“引擎骨架”和“业务模块接入通道”。
但还没有做出一个完整可交付的 ATE 软件产品。
```

## 1. 我们最初想做什么

最初目标是三层解耦：

```text
UI 层
  -> 负责显示、编辑、操作、查看结果
  -> 不应该知道 CAN、DLL、仪器协议、产品细节

任务调度层
  -> 负责读 JSON/config
  -> 负责决定下一步跑什么
  -> 负责 Retry、Cleanup、Barrier、Resource、Loop、结果收集
  -> 不应该写具体项目测试逻辑

业务测试逻辑层
  -> 负责 CAN 解析、仪器命令、产品协议、具体测量
  -> 可以是 C/C++ DLL、Python 脚本、外部 EXE
  -> 把结果用统一格式返回给调度层
```

这个方向目前没有明显偏移。

现在的情况是：

| 层 | 当前状态 | 大白话解释 |
|----|----------|-------------|
| UI 层 | 基本没开始 | 现在没有真正的操作界面，CLI 暂时代替 UI 验证引擎。 |
| 调度层 | MVP 已经有了 | 能编译 JSON、跑步骤、多 UUT、Retry、Cleanup、Barrier、Resource、Loop。 |
| 业务逻辑层 | 第一条接入链路已经通了 | Python、外部进程、NativeHost DLL、模拟 CAN DLL 都跑通过。 |
| 硬件连接层 | 核心模型刚开始 | `DeviceSessionManager`、Station config JSON、`StationRuntime`、CLI `--station`、`PersistentQProcessTransport` 和 fake instrument host 已能管理并加载逻辑设备配置/session，并验证长驻进程跨 step 保持状态；真实驱动和生产级 Host 协议还没做。 |

所以框架方向没歪，但项目状态仍然是“调度引擎原型”阶段，不是“ATE 成品软件”阶段。

## 2. 现在实际能做什么

现在 build 之后，可以通过 CLI 跑这些示例：

```powershell
PicoATE.Cli.exe run examples/simple_sequence.json
PicoATE.Cli.exe run examples/basic_sequence.json --uuts 2
PicoATE.Cli.exe run examples/custom_disabled_sequence.json
PicoATE.Cli.exe run examples/for_loop_sequence.json
PicoATE.Cli.exe run examples/external_echo_sequence.json
PicoATE.Cli.exe run examples/python_echo_sequence.json
PicoATE.Cli.exe run examples/nativehost_dll_sequence.json
PicoATE.Cli.exe run examples/can_dll_sequence.json
PicoATE.Cli.exe run examples/persistent_instrument_sequence.json
PicoATE.Cli.exe run examples/dmm_can_adapter_sequence.json --station examples/stations/basic_station.json
```

这些命令背后做的事情是：

1. 读取一个 JSON 测试流程文件。
2. 把 JSON 解析成编辑期模型 `SequenceDef`。
3. 把 `SequenceDef` 编译成不可变的 `ExecutionPlan`。
4. 创建一个或多个 UUT 的运行状态。
5. 调度器开始跑图。
6. 普通步骤、等待步骤、Action 步骤、Barrier、Loop、Cleanup 都按规则执行。
7. Action 步骤通过 `moduleId` 找到业务模块。
8. 业务模块返回 Passed / Failed / Error / Timeout、输出值、测量值。
9. 调度器记录每次 attempt。
10. 最后生成 `ExecutionReport`。
11. CLI 打印运行结果。

所以它已经能跑一条完整闭环：

```text
JSON 流程
  -> 编译
  -> 调度执行
  -> 调业务模块
  -> 收结果
  -> 输出报告 DTO
```

但这个闭环现在主要通过 CLI 和测试验证，还没有 UI。

## 3. 当前工程结构

现在项目主要目录是：

```text
src/core
  核心调度引擎和运行时库

src/cli
  命令行 demo

src/mockhost
  模拟外部进程，用来测试 QProcessTransport

src/nativehost
  独立进程，用来加载 C/C++ DLL

src/testdllmodule
  测试 DLL，验证 PicoATE_Execute ABI

src/canexamplemodule
  纯软件 CAN 解码 DLL 示例

tests
  QtTest 测试

examples
  JSON 示例、Python 示例、NativeHost manifest 示例

docs
  架构、合同、schema、进度、当前拆解文档
```

这说明现在已经是一个真实工程，不是几个零散代码文件。

当前已经可以：

- 用 VS2022 + Qt6 + CMake 配置
- 编译
- 跑 QtTest
- 跑 CLI 示例

还没有：

- 安装包
- 工站部署目录规范
- 项目模板
- UI 工程
- 模块打包规范

## 4. JSON 测试流程能力

### 4.1 现在支持什么

现在 Sequence JSON 支持：

- sequence `id`
- sequence `name`
- sequence `version`
- `metadata`
- `moduleBindings`
- groups
- steps
- resources
- retry
- timeout
- errorPolicy
- barrier
- loop
- checkpointBefore / checkpointAfter
- disabled step / disabled group
- unknown field warning

支持的 group 类型：

| 类型 | 作用 |
|------|------|
| `setup` | 前置步骤，比如夹具打开、电源上电。 |
| `main` | 主测试流程。 |
| `custom` | 自定义主流程分组，可以插在 body 执行链里。 |
| `cleanup` | 收尾清理，比如断电、关闭夹具、释放资源。 |

支持的 step 类型：

| 类型 | 当前效果 |
|------|----------|
| `noop` | 什么都不做，直接通过。 |
| `wait` | 等待指定毫秒。 |
| `action` / `mockAction` | 调用业务模块。 |
| `barrier` | 多 UUT 同步点。 |
| `cleanup` | 清理步骤，默认 alwaysRun。 |
| `loop` / `forLoop` | 调度层控制的固定 for 循环。 |
| `statement` | 目前先降级成 Action，不是真正表达式引擎。 |
| `sequenceCall` | 目前先降级成 Action，不是真正嵌套 Sequence。 |

### 4.2 这意味着什么

现在已经可以用 JSON 描述类似这样的流程：

```text
Setup:
  - 打开夹具
  - 上电

Main:
  - 测电压
  - Retry 失败项
  - 多 UUT 等待 Barrier
  - 循环测 3 次

Cleanup:
  - 下电
  - 关闭夹具
```

不需要把测试流程硬编码到 C++。

### 4.3 已经比较稳的地方

- 字段类型错误会报具体路径。
- 不认识的字段会 warning，不会悄悄吞掉。
- disabled step / group 不会进入执行计划。
- custom group 能接入主执行链。
- 示例 JSON 有测试覆盖。

### 4.4 还没完成的地方

- 没有可视化 Sequence 编辑器。
- 没有真正的 JSON Schema 文件给编辑器自动提示。
- `sequenceCall` 还不是真正的子序列调用。
- `statement` 还不是真正表达式脚本。
- JSON 现在能用，但还不是最终形态。

## 5. SequenceCompiler 编译器

### 5.1 现在做什么

编译链路是：

```text
JSON
  -> SequenceCompiler
  -> SequenceDef
  -> PlanBuilder
  -> ExecutionPlan
```

编译器负责：

- 解析 JSON
- 检查字段类型
- 把字符串枚举转成强类型枚举
- 收集错误
- 收集 warning
- 保留中间模型 `SequenceDef`
- 成功后生成 `ExecutionPlan`

### 5.2 效果是什么

如果 JSON 写错，理论上会看到类似：

```text
groups[0].steps[2].timeoutMs: Expected number, got string
```

而不是一堆看不懂的崩溃。

### 5.3 已经有的能力

- 错误带 JSON path。
- 字段类型校验。
- 枚举值校验。
- warning 机制。
- 示例文件全链路编译执行。

### 5.4 还缺什么

- warning as error 模式。
- 稳定 diagnostic code。
- 更完整的 schema。
- 编辑器集成。
- 中文错误消息。

## 6. PlanBuilder 和 ExecutionPlan

### 6.1 它们干什么

`PlanBuilder` 负责把编辑模型变成执行图。

大概是：

```text
SequenceDef / StepDef / StepGroupDef
  -> ExecNode
  -> ExecEdge
  -> CleanupRegion
  -> LoopRegion
  -> ExecutionPlan
```

`ExecutionPlan` 是调度器真正看的东西。

它不是 JSON，不关心 JSON 格式，只关心：

```text
有哪些节点
节点之间怎么连
哪个是入口
哪个是出口
哪些节点属于 cleanup
哪些节点属于 loop body
```

### 6.2 已经支持什么

- Setup/Main/Custom/Cleanup 分组编译。
- 同组内串行边。
- 组之间桥接边。
- Main 到 Cleanup 的 Finally 边。
- CleanupRegion。
- LoopRegion。
- disabled step/group 跳过。
- 自动给普通步骤挂 cleanupRegionId。

### 6.3 效果是什么

用户写的是这种直观 JSON：

```text
groups -> steps
```

调度器看到的是执行图：

```text
node A -> node B -> node C
                  -> cleanup
```

这个分层是对的。

### 6.4 还没做什么

- 子序列 `SequenceCall` 还没有真正展开。
- 动态重编译没有做。
- 大 plan 的性能/内存优化没有做。

目前这块属于 MVP 可用。

## 7. ExecutionSession

### 7.1 它是什么

`ExecutionSession` 可以理解成一次运行会话。

它负责把这些东西组织起来：

- 一个 `ExecutionPlan`
- 多个 UUT
- 调度器
- 模块注册表
- 资源管理器
- BarrierController
- LoopController
- NodeRunner

### 7.2 它现在能做什么

- 跑一个 UUT。
- 跑多个 UUT。
- 注册模块。
- 请求 stop。
- 返回 `ExecutionSessionResult`。
- 生成 `ExecutionReport`。

### 7.3 效果是什么

CLI 不需要自己理解图调度细节。

CLI 只要说：

```text
请用这个 plan 跑这些 UUT
```

然后拿结果。

### 7.4 还没完成什么

- 没有暂停/继续。
- 没有单步调试。
- 没有真正异步事件流。
- 没有崩溃后恢复。
- 没有长时间工站 session 管理。

所以现在是“可运行会话”，不是“完整生产会话系统”。

## 8. 调度器核心

### 8.1 它现在做什么

调度器大概做这些事：

```text
找 ready 节点
执行节点
记录结果
根据结果决定 retry / cleanup / stop / continue
处理 barrier
处理 resource
处理 loop
释放后续节点
直到结束
```

### 8.2 已有能力

- 节点依赖判断。
- ready queue。
- node attempt。
- retry。
- cleanup 激活。
- stop 后 cleanup。
- barrier release。
- failed UUT before barrier。
- loop body release。
- resource block/release。

### 8.3 这说明什么

它已经不是简单的“for 循环跑 step list”。

它已经具备图调度器雏形。

### 8.4 还不是完整的地方

- 不是复杂异步调度器。
- 没有优先级算法。
- 没有饥饿预防。
- 没有可插拔 RunStrategy。
- 没有生产级调度时间线。
- 并行执行策略还比较基础。

## 9. Retry 和错误策略

### 9.1 支持什么

每个步骤可以配置：

- onFail
- onError
- onTimeout

动作可以是：

| 动作 | 意思 |
|------|------|
| `Continue` | 继续跑后面的。 |
| `StopUut` | 停掉当前 UUT。 |
| `Retry` | 当前节点再跑一次。 |
| `RunCleanup` | 进入 cleanup。 |
| `Abort` | 更强的中止。 |

### 9.2 效果是什么

比如一个测量失败，可以配置：

```text
失败 -> retry 2 次
还是失败 -> cleanup
```

调度器会记录 attempt。

### 9.3 已经比较好的地方

- attempt 能在 report 里看到。
- fail/error/timeout 是分开的策略。
- 业务模块不需要自己处理 retry。

### 9.4 还缺什么

- 没有 step -> group -> sequence -> station 多级 fallback。
- 没有复杂错误分类。
- 没有 UI 配置。
- 没有错误码体系。

## 10. Cleanup

### 10.1 现在做什么

Cleanup 是收尾清理。

可以在这些情况下触发：

- 正常完成
- step failed
- module error
- timeout
- user stop
- user abort

cleanup group 和 cleanup step 默认 alwaysRun。

### 10.2 效果是什么

比如测试失败或者用户 stop：

```text
不要直接停在那里
先跑下电、关夹具、释放资源等 cleanup
```

### 10.3 已有能力

- CleanupRegion。
- Finally 边。
- stop -> cleanup 的测试。
- failure -> cleanup 的测试。

### 10.4 还缺什么

- cleanup 的结果展示还不够丰富。
- cleanup 和资源回滚还不是事务。
- cleanup 和 checkpoint 恢复还没打通。

## 11. ResourceManager

### 11.1 现在做什么

ResourceManager 管资源。

比如：

```text
DMM1
PowerSupply1
CAN_Channel_0
FixtureLock
```

当多个 UUT 抢同一个资源时：

```text
UUT-1 拿到 DMM1
UUT-2 等待 DMM1
UUT-1 释放
UUT-2 继续
```

### 11.2 已有能力

- 资源申请。
- 资源租约。
- 等待队列。
- waiters 可 snapshot。

### 11.3 效果是什么

多 UUT 运行时，可以避免两个 UUT 同时使用互斥资源。

### 11.4 还缺什么

- 没有资源策略插件。
- 没有资源健康状态。
- 没有真实工站资源拓扑。
- 没有资源事务回滚。
- 没有资源仪表板。

## 12. BarrierController

### 12.1 现在做什么

Barrier 是多 UUT 同步点。

比如：

```text
UUT-1 到 barrier
UUT-2 到 barrier
两者都到了
一起继续
```

### 12.2 已有能力

- 成员到达。
- 等待期望 UUT 数量。
- release decision。
- failed member before arrival。
- drop failed member。
- snapshot 结构。

### 12.3 效果是什么

可以支持 batch 测试里一些“大家都到这里再继续”的场景。

### 12.4 还缺什么

- 更复杂的产线策略。
- 先到者帮助后到者这种资源协作。
- barrier 时间线诊断。
- UI 展示。

现在 barrier 是 MVP，不是最终产线调度策略。

## 13. LoopController

### 13.1 现在支持什么

支持固定 for loop。

示例：

```json
{
  "kind": "loop",
  "loop": {
    "variable": "sampleIndex",
    "from": 0,
    "to": 2,
    "step": 1
  },
  "steps": [
    {
      "id": "measure-sample",
      "kind": "action"
    }
  ]
}
```

实际效果：

```text
sampleIndex = 0 -> 跑 body
sampleIndex = 1 -> 跑 body
sampleIndex = 2 -> 跑 body
```

### 13.2 已有能力

- 调度层管理 loop。
- 每个 UUT 有自己的 loop cursor。
- loop value 写入 runtime variables。
- Plan 保持不可变。
- `ExecutionReport` 会标出 loop body step 归属，并在每个 attempt 上记录 iteration number/value。

### 13.3 这点为什么重要

业务模块不需要知道自己在循环里。

它只接收输入：

```json
{
  "sampleIndex": 1
}
```

### 13.4 还缺什么

- 没有 while。
- 没有 foreach array。
- 没有 break / continue。

Loop 的下一步不是“让 report 看得懂第几轮”，这点已经补上；下一步更偏向 while/foreach/break/continue 这类更复杂控制流。

## 14. Runtime 变量替换

### 14.1 现在支持什么

Action 执行前，会解析输入里的变量。

支持：

```text
${var.sampleIndex}
${loop.index}
${loop.value}
${uut.id}
${attempt.index}
${attempt.number}
```

### 14.2 效果是什么

JSON 里可以写：

```json
{
  "inputs": {
    "sampleIndex": "${var.sampleIndex}",
    "label": "sample-${var.sampleIndex}",
    "uutId": "${uut.id}"
  }
}
```

跑到不同 loop/UUT/attempt 时，值会不一样。

### 14.3 已有能力

- 配置期变量替换。
- 运行期变量替换。
- Map/List 递归替换。
- 整个字段是变量时保留原类型。
- 嵌在字符串里时变成字符串。

### 14.4 还缺什么

- 没有表达式语言。
- 没有算术表达式。
- 没有变量调试窗口。
- 没有复杂作用域模型。

## 15. 业务模块边界

### 15.1 现在的核心接口

业务模块实现 `IModule`：

```cpp
class IModule {
public:
    virtual ModuleId moduleId() const = 0;
    virtual ModuleResult execute(const ModuleFunction& functionName,
                                 const ModuleExecutionContext& context) = 0;
};
```

它返回 `ModuleResult`：

```text
outcome
outputs
measurements
errorCode
errorMessage
```

### 15.2 这意味着什么

调度器不直接知道：

- CAN 怎么解析
- DMM 怎么读
- 电源怎么控制
- 产品协议怎么写

调度器只知道：

```text
我要调用 moduleId = project.can
function = verifyPackVoltage
inputs = {...}
```

模块返回：

```text
Passed / Failed / Error / Timeout
outputs
measurements
```

### 15.3 已有能力

- ModuleRegistry。
- NodeRunner 通过 moduleId 找模块。
- 缺模块会报错。
- ModuleResult 映射成 NodeResult。

### 15.4 还缺什么

- 没有模块管理 UI。
- 没有模块包格式。
- 没有模块版本协商。
- 没有模块 capability 列表。

## 16. 外部进程模块

### 16.1 现在怎么工作

`QProcessTransport` 可以启动一个外部程序。

通信方式是：

```text
PicoATE -> stdin 写一行 JSON
外部程序 -> stdout 返回一行 JSON
stderr -> 诊断信息
```

### 16.2 效果是什么

项目团队可以写任意语言的 EXE，只要它会读写这个 JSON 协议。

例如 Python：

```text
PicoATE
  -> QProcessTransport
  -> python.exe examples/modules/echo_module.py
  -> 返回 JSON
```

### 16.3 已有能力

- 正常响应。
- timeout。
- 进程非 0 退出。
- crash exit。
- 空输出。
- 非法 JSON。

### 16.4 还缺什么

- 现在是每次调用启动一个新进程。
- 没有长驻进程。
- 没有进程池。
- 日志诊断还比较基础。
- 不适合跨多个 step 长时间保持硬件句柄。

## 17. DLL 加载能力

这部分要讲清楚，因为最容易误解。

### 17.1 现在不是“任意 DLL 函数随便调”

PicoATE 现在不能直接理解一个厂家 DLL 里有哪些函数。

比如厂家 DLL 里有：

```cpp
extern "C" __declspec(dllexport) int CAN_Open(int channel);
extern "C" __declspec(dllexport) int CAN_Read(int channel, unsigned char* data, int len);
extern "C" __declspec(dllexport) int CAN_Close(int channel);
```

PicoATE 现在不会自动知道：

- 该调哪个函数
- 参数怎么传
- 指针怎么分配
- 返回值怎么解释
- handle 怎么管理

所以不能说“任意 DLL 直接加载就能用”。

更准确说法是：

```text
PicoATE 可以加载符合 PicoATE ABI 的 C/C++ 适配 DLL。
```

### 17.2 PicoATE ABI 是什么

DLL 需要导出这个函数：

```cpp
extern "C" __declspec(dllexport)
int PicoATE_Execute(
    const char* requestJsonUtf8,
    char* responseJsonUtf8,
    int responseBufferSize);
```

PicoATE 只调用这个统一入口。

输入是 JSON。

输出也是 JSON。

### 17.3 如果是厂家 CAN DLL，应该怎么接

不要改厂家 DLL。

应该写一个项目适配 DLL：

```text
PicoATE
  -> PicoATE.NativeHost.exe
    -> ProjectCanAdapter.dll
      -> VendorCanDriver.dll
        -> CAN_Open / CAN_Read / CAN_Close
```

`ProjectCanAdapter.dll` 对外导出 `PicoATE_Execute`。

里面根据 JSON 的 `function` 决定调厂家哪个函数。

比如：

```text
function = "readFrame"
  -> CAN_Open
  -> CAN_Read
  -> CAN_Close
  -> 返回 measurement
```

### 17.4 当前最稳的 DLL 使用方式

现在最稳的是“一次业务调用完成一件测试意图”：

```text
Step: VerifyPackVoltage
  -> adapter 内部 open
  -> read
  -> parse
  -> close
  -> 返回 Passed/Failed 和测量值
```

现在不太适合这种跨 step 的写法：

```text
Setup: CAN_Open
Main:  CAN_Read
Main:  CAN_Read
Cleanup: CAN_Close
```

原因是当前 `QProcessTransport` 是每个调用启动一个短进程。

也就是说：

```text
Open 那一步的进程结束后，设备 handle 可能就没了。
Read 下一步是另一个进程，不一定拿得到上一步的状态。
```

如果要支持这种“打开一次，读很多次，最后关闭”的真实硬件模式，需要做长驻 Host / Persistent Transport。

这是当前一个很关键的缺口。

## 18. NativeHost

### 18.1 它是什么

`PicoATE.NativeHost.exe` 是一个独立进程。

它负责：

```text
接收 PicoATE 发来的 JSON
加载 DLL
调用 DLL 的 PicoATE_Execute
把 DLL 返回的 JSON 再传回 PicoATE
```

### 18.2 为什么要有它

如果 DLL 直接在主进程里跑，DLL 崩溃可能把 PicoATE 主程序也带崩。

NativeHost 把 DLL 放到子进程里。

这样 DLL 卡死/崩溃时，父进程可以 timeout/kill 子进程。

### 18.3 已有能力

- `--dll` 旧模式。
- `--manifest` 推荐模式。
- DLL 路径变量替换。
- symbol 配置。
- bufferSize 配置。
- dllTimeoutMs 配置。
- 端到端测试。

### 18.4 还缺什么

- 不是长驻 session host。
- 没有模块版本协商。
- 没有 DLL capability 查询。
- 诊断信息还不够强。
- 没有正式项目打包规范。

## 19. NativeHost Manifest

### 19.1 现在是什么

Manifest 是 NativeHost 的 DLL 加载配置。

示例：

```json
{
  "dll": "${PICOATE_CAN_DLL}",
  "symbol": "PicoATE_Execute",
  "bufferSize": 65536,
  "dllTimeoutMs": 30000,
  "metadata": {
    "name": "PicoATE Simulated CAN Decode DLL"
  }
}
```

### 19.2 它解决什么

不用把 DLL 路径、symbol、buffer size 写死在代码里。

不同项目可以换 manifest。

### 19.3 它不应该放什么

Manifest 不应该放调度语义。

比如这些不该放 manifest：

- Retry
- Cleanup
- Step 顺序
- Barrier
- Resource
- 测试流程

这些应该放 Sequence JSON。

## 20. Measurement / Limit 模型

### 20.1 现在支持什么

现在测量值是正式 DTO：

```text
MeasurementResult
  name
  value
  unit
  rawValue
  lowerLimit
  upperLimit
  status
  errorCode
  errorMessage
  attributes
```

业务模块可以返回：

```json
{
  "measurements": {
    "name": "PackVoltage",
    "value": 100.0,
    "unit": "V",
    "lowerLimit": 95,
    "upperLimit": 105,
    "status": "Passed"
  }
}
```

### 20.2 效果是什么

CLI 可以打印类似：

```text
measurement PackVoltage = 100 V [95, 105] Passed
```

未来 UI 和报表不用从 outputs 里面乱翻。

### 20.3 已经打通到哪里

- ModuleTransportResponse。
- ModuleResult。
- NodeResult。
- AttemptReport。
- StepReport。
- ExecutionReport。
- CLI。

### 20.4 还缺什么

- 没有最终报表导出。
- 没有数据库。
- 没有图表。
- 没有限值库。
- 没有单位换算。
- 没有复杂统计。

## 21. ExecutionReport

### 21.1 它是什么

`ExecutionReport` 是给 UI/CLI/报告看的只读结果模型。

它包括：

- planId
- session state
- completed
- hasError
- UUT 列表
- Step 列表
- Attempt 列表
- measurements

### 21.2 为什么要有它

避免 UI 或 CLI 直接读内部 runtime 状态。

内部状态是调度器自己维护的。

外部消费者应该看一份整理好的结果 DTO。

### 21.3 已有能力

- retry attempt 能看到。
- step outcome 能看到。
- measurement 能看到。
- loop body 归属和每次 attempt 的 loop iteration 能看到。
- wasError 能帮助判断是否错误。

### 21.4 还缺什么

- skip reason 不够明确。
- cleanup/resource/barrier 历史不够明确。
- 没有最终报表文件。

## 22. CLI

### 22.1 现在能做什么

CLI 能：

- 读取 JSON。
- 编译 sequence。
- 打印 compile error。
- 打印 warning。
- 注册 moduleBindings。
- 运行一个或多个 UUT。
- 打印 step/attempt。
- 打印 measurement。

### 22.2 它现在的角色

CLI 是当前的“临时 UI”。

它主要用来证明引擎能跑，不是最终操作员界面。

### 22.3 还缺什么

- 没有实时进度 UI。
- 没有按钮操作。
- 没有暂停/继续。
- 没有 sequence 编辑。
- 没有图形报告。

## 23. 模拟 CAN DLL 示例

### 23.1 它做什么

`PicoATE.CanExampleModule.dll` 是一个纯软件 DLL。

它可以：

- 从 JSON 读取 raw bytes。
- 按配置解析成信号。
- 做 scale。
- 生成 PackVoltage 之类的值。
- 做上下限判断。
- 返回 measurement。

### 23.2 它证明了什么

它证明：

```text
JSON 配置
  -> NativeHost
  -> DLL
  -> 协议解析
  -> 测量值
  -> 限值判断
  -> 报告
```

这条链路可行。

### 23.3 它没有证明什么

它没有证明：

- 真实 CAN 分析仪能跑。
- 厂家 CAN DLL 能直接跑。
- 真实硬件连接生命周期已经解决。
- 生产现场稳定性已经足够。

它只是软件层面的边界验证。

## 24. Session Snapshot / 持久化

### 24.1 现在有什么

现在有一些 snapshot 方向的结构和考虑。

比如：

- Resource waiters 可以序列化。
- Barrier 状态有 snapshot 思路。
- ExecutionSession 有 snapshot 骨架。

### 24.2 现在没有什么

现在还没有完整的：

- 保存 session 到磁盘。
- 从磁盘恢复 session。
- 程序崩溃后继续跑。
- 调试会话保存。
- 单个 UUT 移出后恢复。
- Checkpoint 文件格式。

所以这块目前不是完成能力，只是架构预留。

## 24.5 DeviceSessionManager / 硬件连接生命周期

### 24.5.1 现在有什么

现在已经新增 `DeviceSessionManager` 核心模型。

它负责：

- 注册设备 session factory。
- 配置逻辑设备。
- 根据逻辑设备 ID 打开 session。
- 已连接时复用 session。
- closeSession。
- closeAll。
- 查询设备状态。
- 缺 driver 或 connect 失败时返回明确错误。
- station config JSON 解析。
- station config 变量替换。
- station config 初始化 `DeviceSessionManager`。
- `StationRuntime` 统一持有 station config 和 `DeviceSessionManager`。
- CLI `--station` 可以在运行 sequence 前加载 station config 并打印 station/device 摘要。
- `PersistentQProcessTransport` 可以复用同一个外部进程。
- `PicoATE.FakeInstrumentHost.exe` 可以验证 `open -> read -> read -> cleanup close` 跨 step 状态保持。
- Fake Instrument Host 现在也支持 `health/reconnect/configureDcv/identity/readFrame/shutdown`。
- cleanup step 如果带 `moduleId`，会真正执行模块，可用于真实硬件收尾。
- `ModuleExecutionContext` 会注入 `IModuleRuntimeServices`。
- 业务模块可以通过 `DMM1/CAN1` 逻辑 ID 打开 session 或调用设备命令。
- `TransportDeviceSession` 可以把 station config 里的 `fake.dmm/fake.can` 映射到长驻 host。
- 内置 `example.dmm` / `example.can` 已验证 DMM/CAN adapter 的最小形状。

大白话说：

```text
DeviceSessionManager 管“DMM1/CAN1/PSU1 到底连没连”。
ResourceManager 管“当前谁能用 DMM1/CAN1/PSU1”。
```

这两个不是一回事。

### 24.5.2 为什么重要

真实 DMM/CAN/电源通常不适合每个 step 都 open/close。

更合理的是：

```text
工站初始化或运行开始:
  Connect DMM1

测试步骤:
  申请 DMM1 使用权
  Configure / Read
  释放 DMM1 使用权

工站关闭或运行 cleanup:
  Disconnect DMM1
```

### 24.5.3 还缺什么

现在还没有：

- 真实 DMM/CAN/PSU driver。
- 生产级 Persistent Instrument Host 诊断、能力查询、版本协商。
- 真实硬件长时间稳定性验证。
- ResourceManager 和 DeviceSessionManager 的自动联动。

所以它现在是“连接生命周期核心模型 + fake 长驻 Host 验证 + 业务模块设备访问入口”，还不是完整真实硬件接入系统。

## 25. 测试覆盖

### 25.1 现在覆盖了什么

QtTest 里覆盖了很多方向：

- ResourceManager。
- BarrierController。
- PlanCache。
- NodeRunner。
- ModuleRuntime。
- ModuleTransportJson。
- VariableResolver。
- RuntimeVariableResolver。
- QProcessTransport。
- DllBridgeInvoker。
- NativeHost。
- NativeHost Manifest。
- 模拟 CAN DLL。
- Scheduler retry/cleanup/stop/barrier。
- SequenceDef。
- PlanBuilder。
- SequenceCompiler。
- 示例 JSON。
- ExecutionReport。

### 25.2 这说明什么

现在不是完全没测试的 demo。

很多核心行为有自动化验证。

### 25.3 还没有覆盖什么

- 真实硬件。
- 真实厂家 DLL。
- 长时间稳定性。
- 高并发。
- UI。
- 崩溃恢复。
- 生产报表。

## 26. 当前成熟度估计

这不是承诺，只是工程判断。

| 方向 | 估计成熟度 | 解释 |
|------|------------|------|
| 三层解耦方向 | 80% | 边界已经比较清楚，代码也基本按这个方向走。 |
| 调度内核 | 70%-75% | 核心机制能跑，并已有 station runtime 雏形，但还不是生产级运行平台。 |
| JSON 配置流程 | 75%-80% | 能用、能测，但缺编辑器/schema/更复杂语义。 |
| 业务模块边界 | 80%-85% | Python、EXE、DLL、NativeHost 路径都通了，但缺包装、版本、长连接。 |
| 硬件连接生命周期 | 60%-65% | `DeviceSessionManager`、Station config、StationRuntime、CLI `--station`、fake persistent host、module-backed cleanup、runtimeServices 和 DMM/CAN adapter spike 已落地，但生产级 host 诊断、真实 driver 和稳定性验证还没做。 |
| Measurement 模型 | 65%-70% | DTO 有了，但没有报表/数据库/UI。 |
| 持久化恢复 | 15%-25% | 有预留，没实现完整功能。 |
| UI/ViewModel | 5%-10% | 基本没开始。 |
| 产品完成度 | 35%-45% | 是一个不错的引擎原型，不是完整产品。 |

## 27. 可能让你觉得“不太对劲”的地方

### 27.1 我们先做了很多底层，UI 还没有影子

这不一定错。

但它会导致一个感觉：

```text
明明做了很多东西，却看不到一个真正软件长什么样。
```

所以你现在感觉不踏实是正常的。

### 27.2 DLL 支持容易被误解

现在支持的是：

```text
符合 PicoATE_Execute ABI 的适配 DLL
```

不是：

```text
随便拿一个厂家 DLL，里面有什么函数我都能直接调
```

这个说法必须统一，不然未来会误判工作量。

### 27.3 真实硬件连接生命周期还没完全解决

真实硬件经常是：

```text
Open
配置
读很多次
写很多次
Close
```

现在已经有 `DeviceSessionManager` 核心模型，可以表达逻辑设备 session 和连接复用。

也已经有 `PersistentQProcessTransport` 和 `PicoATE.FakeInstrumentHost.exe`，可以验证一个长驻进程里的状态跨 step 保持。

但普通 `QProcessTransport` 仍然是短进程调用。

这对纯算法、协议解析、一次性动作没问题。

对长连接仪器/驱动来说，后面还要把 fake host 继续深化成生产级协议：

```text
Production Persistent Host
长驻进程
设备 session
handle 生命周期
health check / reconnect / shutdown
```

所以剩下的关键缺口是生产级 Persistent Instrument Host 诊断/稳定性、ResourceManager 与设备状态联动、真实 driver 接入。

### 27.4 Loop 能跑，报告已经补了 iteration，但控制流还很基础

Loop 执行本身有了。

现在报告里已经能表达：

```text
第 0 轮结果
第 1 轮结果
第 2 轮结果
```

UI 不需要再从 attempt 顺序猜第几轮。

但 Loop 仍然只有固定 for-loop，没有 while、foreach、break、continue。

### 27.5 Persistence 还只是方向

文档里一直强调 checkpoint/recovery，但代码目前没有真正完成这个能力。

所以不能对外说：

```text
已经支持断点恢复
```

只能说：

```text
架构上开始预留，但功能未完成
```

### 27.6 Resource 和 Barrier 是 MVP

现在能处理基本场景。

但真实产线可能有：

- 复杂工站资源
- 多仪器共享
- 批量测试节拍
- 某 UUT 掉队
- 资源故障
- 产线继续流转

这些还没完全展开。

### 27.7 “比 TestStand 好用”现在还只是目标

目前不能说已经比 TestStand 好。

现在更准确的说法是：

```text
我们在搭一个更轻、更可控、更适合自己项目的 ATE 调度内核。
```

是否更好用，要等 UI、真实项目接入、报表、调试体验做出来之后再判断。

## 28. 我认为现在应该重点复盘的问题

### 28.1 真实硬件模块到底应该怎么活着

这是最重要的问题。

我们要决定：

```text
业务模块是一次调用一次结束？
还是应该有长驻 host，一直持有设备连接？
```

我现在倾向于两种都要：

```text
短调用模块：
  适合纯算法、协议解析、一次性动作、简单脚本

长驻模块：
  适合 CAN、DMM、电源、扫码枪、夹具控制等真实硬件
```

如果这个不想清楚，后面接真实 CAN DLL 时会卡住。

### 28.2 Step 应该表达“驱动函数”还是“测试意图”

不太好的风格：

```text
Step 1: CAN_Open
Step 2: CAN_Read
Step 3: CAN_Close
```

更像测试系统的风格：

```text
Step: Verify PackVoltage
  -> 模块内部自己 open/read/parse/close 或使用已有连接
```

这个决定会影响整个系统好不好用。

### 28.3 JSON 里到底应该放多少逻辑

JSON 应该配置：

- 测试流程
- 测试项
- 参数
- 限值
- 模块绑定

JSON 不应该变成一门巨大的脚本语言。

如果 JSON 越来越像代码，最后可能会变得比 TestStand 还难用。

### 28.4 UI 什么时候开始

太早做 UI，会把还没稳定的模型固化。

太晚做 UI，又看不到真实使用体验。

我建议：

```text
先做薄 ViewModel + 简单 Runner UI。
不要马上做完整 Sequence 编辑器。
```

第一版 UI 应该只做：

- 打开 JSON。
- 显示编译错误/warning。
- 运行。
- 显示 UUT 状态。
- 显示 step/attempt。
- 显示 measurement。
- 显示最终结果。

这样可以尽早看到产品形状，但不会被 UI 复杂度拖死。

## 29. 当前能做和不能做清单

### 29.1 现在能做

- VS2022/Qt6/CMake 编译。
- 跑 QtTest。
- 读取 JSON sequence。
- 校验 JSON 字段类型。
- 输出 compile warning。
- 生成不可变 ExecutionPlan。
- 跑 Setup/Main/Custom/Cleanup。
- 跳过 disabled step/group。
- 调用 action module。
- 多 UUT 执行。
- Retry。
- Cleanup。
- Stop 后 cleanup。
- Barrier。
- Resource 等待。
- 固定 for loop。
- Runtime 变量替换。
- 从 JSON moduleBindings 注册外部模块。
- 调外部 EXE。
- 调 Python 脚本。
- 通过 NativeHost 调 C/C++ DLL。
- 通过 manifest 配置 DLL。
- 跑模拟 CAN DLL。
- 解析 station config。
- 通过 CLI `--station` 加载工站设备配置。
- 在 `StationRuntime` 中持有 station config 和 `DeviceSessionManager`。
- 通过 `persistent-qprocess` 调长驻外部进程。
- 跑 fake instrument host 示例，验证同一个进程内的 open/read/read/close 状态保持。
- 业务模块通过 `ModuleExecutionContext.runtimeServices` 按逻辑设备 ID 使用设备 session/proxy。
- 跑 DMM/CAN adapter 示例，验证 `ConnectDMM/ConfigureDCV/ReadDMM/Disconnect` 和 CAN `ReadFrame` 风格。
- 返回结构化 measurement。
- CLI 打印结果。
- 生成 ExecutionReport。

### 29.2 现在不能做

- 没有真实 UI。
- 没有可视化 sequence 编辑器。
- 不能直接随便调用厂家 DLL 任意函数。
- 不能跨短进程 step 保持硬件 handle。
- 已有 fake 长驻 Host 验证路径，但没有生产级真实硬件 Host。
- 没有崩溃恢复。
- 没有生产报表。
- station config 已能接 CLI/StationRuntime/fake host，但还没有真实硬件稳定性验证。
- 没有用户权限。
- 没有限值数据库。
- 没有真实 CAN 硬件验证。
- 现在还不能替代完整 TestStand。

## 30. 我建议下一步怎么走

我不建议继续盲目堆功能。

建议先按这个顺序：

| 优先级 | 任务 | 为什么 |
|--------|------|--------|
| P0 | 做薄 ViewModel + 简单 Runner UI | 尽快看到产品样子，但不急着做完整编辑器。 |
| P0 | UI 设备状态展示 | 把 station、device、session、health、运行结果显示出来。 |
| P1 | 增强 QProcess/NativeHost/PersistentHost 诊断 | 接真实 DLL/仪器 Host 时，错误信息不清楚会非常痛苦。 |
| P1 | 做一个 Vendor 风格 DLL/Host 适配层 spike | 模拟或接入真实 Open/Read/Close 厂家接口，看我们边界是否真的舒服。 |
| P3 | 再设计 checkpoint/recovery | 重要，但不应该抢在硬件生命周期前面。 |

## 31. 我的真实判断

这段时间做的东西不是白做。

现在已经证明：

```text
JSON 可以驱动流程。
调度层可以独立跑。
业务模块可以外接。
业务模块可以按逻辑设备 ID 访问设备 session/proxy。
DLL/Python/EXE 可以走统一边界。
测量结果可以结构化返回。
```

这些都是核心地基。

但现在最大的问题不是“能不能跑一个业务模块”。

这个已经基本证明了。

现在最大的问题是：

```text
真实 UI 怎么把这些能力变成一个好用的软件？
```

同时，真实 CAN、仪器、夹具这类东西仍然要继续验证生产级 Host 和真实 driver。

如果它们都只是一次性函数调用，那当前架构就够继续走。

如果它们需要长时间持有连接、跨 step 保持状态，现在已经补上第一条 fake 验证路径：

```text
PersistentQProcessTransport
Fake Instrument Host
module-backed cleanup
runtimeServices -> DeviceSessionManager -> TransportDeviceSession
open -> read -> read -> close 状态保持
```

下一步还要继续盯：

```text
Qt Runner UI / ExecutionViewModel
生产级 Host 诊断
资源和设备状态绑定
真实 driver 稳定性
更好的诊断
```

## 32. 建议你重点看哪些文件

| 方向 | 文件 |
|------|------|
| 模块接口 | `src/core/include/PicoATE/Core/ModuleRuntime.h` |
| 设备 session | `src/core/include/PicoATE/Core/DeviceSessionManager.h` |
| Transport device session | `src/core/include/PicoATE/Core/DeviceTransportSession.h` |
| DMM/CAN adapter spike | `src/core/include/PicoATE/Core/InstrumentAdapterModules.h` |
| 执行计划 | `src/core/include/PicoATE/Core/ExecutionPlan.h` |
| 运行状态 | `src/core/include/PicoATE/Core/RuntimeTypes.h` |
| 会话入口 | `src/core/include/PicoATE/Core/ExecutionSession.h` |
| 调度器 | `src/core/include/PicoATE/Core/ExecutionGraphScheduler.h` |
| JSON 编译器 | `src/core/include/PicoATE/Core/SequenceCompiler.h` |
| 编辑模型 | `src/core/include/PicoATE/Core/SequenceDef.h` |
| PlanBuilder | `src/core/include/PicoATE/Core/PlanBuilder.h` |
| DLL 调用 | `src/core/include/PicoATE/Core/DllBridgeInvoker.h` |
| NativeHost | `src/nativehost/Main.cpp` |
| QProcessTransport | `src/core/include/PicoATE/Core/QProcessTransport.h` |
| 测量模型 | `src/core/include/PicoATE/Core/MeasurementTypes.h` |
| CLI | `src/cli/Main.cpp` |
| 测试 | `tests/CoreTests.cpp` |
| JSON schema 文档 | `docs/sequence_json_schema.md` |
| 模块合同 | `docs/module_contract.md` |
| NativeHost manifest | `docs/nativehost_manifest.md` |
| 项目愿景 | `docs/project_vision.md` |

## 33. 最后一句

如果要非常直白地说：

```text
PicoATE 现在已经有了“骨头”和一部分“神经”。
但还没有长出完整的“脸”和“手脚”。
```

骨头就是调度模型。

神经就是模块边界和 JSON 配置。

脸是 UI。

手脚是真实硬件接入、报表、部署、恢复、工站管理。

所以这不是失败，也不是跑偏。

但确实到了一个需要停下来认真校准方向的阶段。
