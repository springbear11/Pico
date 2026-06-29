# PicoATE Work Summary - 2026-06-22

## 今日目标

今天的目标是继续把 PicoATE 从“可运行调度内核”推进到更接近真实项目交付的形态：

```text
UI 不改
+ 调度框架不改
+ 新增/替换底层业务 DLL 或 Python 脚本
+ 用 JSON/config 描述测试流程、模块绑定和 DLL 加载信息
= 完成项目交付
```

今天完成的重点都围绕这条主线展开。

## 今日完成

| 序号 | 事项 | 结果 |
|------|------|------|
| 1 | LoopController For 循环 | 新增调度层 `LoopController`、`ExecNodeKind::Loop`、`LoopRegion`，支持固定 `for` 循环 JSON 配置 |
| 2 | for-loop 示例 | 新增 `examples/for_loop_sequence.json`，CLI 和 CoreTests 均覆盖 |
| 3 | VariableResolver 中心化 | 抽出公共变量替换能力，支持内置变量、显式变量、环境变量、递归替换、Map/List 深度替换 |
| 4 | ModuleBindingRegistrar 迁移 | `moduleBindings.program/arguments` 改为复用 `VariableResolver`，不再保留私有替换逻辑 |
| 5 | NativeHost manifest | 新增 manifest 解析器，NativeHost 支持 `--manifest`、`--project-dir`、可重复 `--var NAME=VALUE` |
| 6 | NativeHost 示例改造 | `nativehost_dll_sequence.json` 改为通过 manifest 启动 DLL |
| 7 | 文档更新 | 新增/更新 `variable_resolver.md`、`nativehost_manifest.md`、schema、runtime、vision、progress plan |

## 当前验证

最终验证目录：

```text
D:\Work\PicoATE
```

最终验证命令：

```powershell
cmake --build --preset vs2022-qt6-debug
ctest --preset vs2022-qt6-debug
```

最终结果：

```text
8/8 tests passed
```

## 当前完成度

按最终产品目标估算：

| 维度 | 当前完成度 | 说明 |
|------|------------|------|
| 总体平台目标 | 约 45% | 三层解耦方向已打通，但 UI、持久化、产品级诊断还没开始或不完整 |
| 调度内核 MVP | 70%-75% | Plan、Session、Scheduler、Retry、Cleanup、Barrier、Resource、Loop、CLI 已可运行 |
| JSON/config 流程 | 70%-75% | 编译、校验、warning、变量替换、moduleBindings、示例已经形成闭环 |
| 业务模块边界 | 75%左右 | `IModule`、`ModuleResult`、`IModuleTransport`、Python、QProcess、NativeHost、manifest 已有 |
| 报告 DTO | 40%左右 | 有 `ExecutionReport`，但还缺 measurement table、loop iteration、skip/resource/cleanup history |
| Checkpoint/恢复 | 10%左右 | 有 snapshot 概念，真正恢复还没实现 |
| UI/ViewModel | 5%-10% | 有 DTO 方向，UI 还没正式开始 |

这些比例是规划参考，不是发布门槛。

## 关键架构结论

### 1. Loop 属于调度层

Loop 不是业务模块逻辑。`LoopController` 只维护每个 UUT 的循环游标，业务模块仍然只看到普通 action 调用。

当前支持：

- 固定 `for` 循环。
- 正向和反向步进。
- 0 次循环时循环体标记为 `Skipped`。
- Plan 运行期不修改，循环游标在 runtime controller 内维护。

当前限制：

- 不支持嵌套 loop。
- 不支持 while/break/continue。
- loop body 每轮执行暂时记录为同一节点的多次 attempt。

后续需要在 `ExecutionReport` 中显式增加 loop iteration 信息。

### 2. 变量替换必须共用 `VariableResolver`

所有配置类路径和项目变量都应复用 `VariableResolver`，避免每个功能各写一套 `${VAR}` 解析。

当前支持：

- `${PROJECT_DIR}`
- `${SEQUENCE_DIR}`
- 显式变量
- 环境变量
- 变量递归引用
- `QVariantMap` / `QVariantList` / `QStringList` 深度替换
- 带 path 的 unresolved variable 错误

后续 NativeHost manifest、station config、report export path 都应该继续复用它。

### 3. NativeHost manifest 只管 DLL 加载边界

manifest 只描述 NativeHost 如何加载 DLL：

- DLL path
- exported symbol
- response buffer size
- DLL in-host timeout
- metadata

manifest 不应该放：

- step 顺序
- retry/cleanup/error policy
- resource
- barrier
- test item flow
- limit 判定流程

这些仍然属于 sequence JSON 或未来项目配置。

### 4. 项目交付路径越来越清晰

当前推荐交付路径：

```text
Sequence JSON
  -> moduleBindings
  -> QProcessTransport
  -> PicoATE.NativeHost.exe
  -> NativeHost manifest
  -> C/C++ project DLL
  -> ModuleTransportResponse
```

这条路已经通过 `nativehost_dll_sequence.json` 和测试 DLL 跑通。下一步要用一个更像真实项目的 CAN/DLL 示例来证明它。

## 重要文件

| 文件 | 用途 |
|------|------|
| `src/core/include/PicoATE/Core/LoopController.h` | 调度层 loop runtime controller |
| `src/core/include/PicoATE/Core/VariableResolver.h` | 公共变量替换服务 |
| `src/core/include/PicoATE/Core/NativeHostManifest.h` | NativeHost manifest 强类型解析入口 |
| `src/nativehost/Main.cpp` | NativeHost executable，支持 `--manifest` 和 legacy `--dll` |
| `examples/for_loop_sequence.json` | for-loop sequence 示例 |
| `examples/nativehost/test_dll_manifest.json` | NativeHost manifest 示例 |
| `examples/nativehost_dll_sequence.json` | 通过 manifest 调用 NativeHost DLL 的 sequence 示例 |
| `docs/variable_resolver.md` | 变量替换规则 |
| `docs/nativehost_manifest.md` | NativeHost manifest 格式和边界 |
| `docs/progress_plan_2026-06-20.md` | 主进度表 |

## 剩余重点

| 优先级 | 任务 | 说明 |
|--------|------|------|
| P0 | 真实 CAN/DLL 示例 | 用 NativeHost manifest 加载模拟 CAN DLL，JSON 配置报文/信号/期望值，证明真实交付路径 |
| P1 | Measurement / Limit 模型 | 正式定义测量值、单位、上下限、判定结果、报表字段 |
| P1 | ExecutionReport 增强 | 增加 loop iteration、skip reason、cleanup reason、resource history、measurement table |
| P1 | QProcessTransport 稳定性 | stderr 诊断、host 启动失败原因、输出截断、长驻 host/进程池候选设计 |
| P2 | Checkpoint/恢复 | session 持久化、恢复运行、调试断点保存、多 UUT 状态移出 |
| P2 | 条件分支/SequenceCall | 补更完整的流程控制语义 |
| P2 | ResourceManager 策略化 | 优先级、饥饿预防、死锁诊断、不同工位策略 |
| P3 | ViewModel/UI | 基于 `ExecutionReport` 和后续 DTO 做 Qt UI 薄层 |

## 下一步建议

下一步做：

```text
真实 CAN/DLL 示例
```

建议拆法：

1. 新增一个测试 DLL，例如 `PicoATE.CanExampleModule.dll`。
2. DLL 按现有 `PicoATE_Execute(json, out, size)` ABI 实现。
3. 输入包含 CAN frame、messageId、signalName、scale、offset、expected/min/max。
4. DLL 返回 decoded value、unit、raw bytes、measurements。
5. 新增 `examples/nativehost/can_manifest.json`。
6. 新增 `examples/can_dll_sequence.json`。
7. 新增 CoreTests 和 CLI ctest，证明 sequence JSON + manifest + DLL 全链路可跑。

这个示例完成后，PicoATE 的主线会更扎实：真实项目只需要新增业务 DLL 和 JSON/config，而不是改 UI 或调度框架。
