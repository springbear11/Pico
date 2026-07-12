# P1 架构与工程骨架 — 成果整理

## P1.1 架构边界与 V3.1 文档

### 大白话说明
明确 UI、任务调度、业务逻辑三层的边界；定义执行计划（ExecutionPlan）、运行期控制器、扩展点和持久化预留。

### 成果

- [architecture_v3_1.md](D:\Work\ATEdesign\architecture_v3_1.md)（约 1400 行）
  - 定义三层解耦边界：UI 层 / 调度层 / 业务逻辑层，每层明确"可以知道什么、不该知道什么"
  - 定义核心术语：`SequenceDef`（编辑期）→ `ExecutionPlan`（编译期不可变模板）→ `ExecutionFrame`/`NodeActivation`/`NodeAttempt`（运行期可变状态）
  - 编译器与调度器边界：编译器产出不可变 `ExecutionPlan`，Cleanup 由编译器静态建模、由运行期激活
  - Cleanup 精确定义：`CleanupRegion` 编译期静态存在于 Plan，运行时通过 `CleanupReason` 激活
  - 控制边语义：`EdgeTrigger`（OnSuccess / OnFail / OnError / OnTimeout / Finally 等），不把所有边当普通依赖边
  - 不可变 Plan 与运行期动态行为：Loop、Retry、SequenceCall 通过 Frame/Activation/Attempt 表达，不修改 Plan
  - Barrier 四维策略：`BarrierArrivalPolicy` / `BarrierReleasePolicy` / `BarrierFailurePolicy` / `BarrierTimeoutPolicy`
  - 6 个可插拔策略接口：`IRunStrategy` / `ISchedulingPolicy` / `IResourceArbitrationPolicy` / `IErrorPolicyResolver` / `ICheckpointPolicy` / `IRecoveryPolicy`
  - 会话持久化与恢复：`ISessionPersistence` 接口、`ExecutionSessionSnapshot` 结构、`RestoreMode` 枚举
  - 变量快照与资源租约边界：变量可回滚，资源租约不可回滚，硬件副作用靠 Cleanup/Compensation
  - ViewModel 薄适配层轮廓
  - MVP 路线：10 个 Phase，从 Plan 模型到 UI 接入

- [architecture_v2.md](D:\Work\ATEdesign\architecture_v2.md)（约 820 行）
  - DAG 执行图、SequenceCompiler 编译管道、强类型系统、流式 ResultStream、统一 JSON-RPC 模块协议
  - L1/L2/L3 三级模块接入方案

- [architecture_v1.md](D:\Work\ATEdesign\architecture_v1.md)（约 740 行）
  - 6 大领域初始拆分：序列引擎、执行运行时、模块适配、结果数据、UI、基础设施

- [ui_layout_v1.md](D:\Work\ATEdesign\ui_layout_v1.md)（约 340 行）
  - Sequence Editor / Monitor / ResultViewer / Variables / Station Config 的 UI Mockup

### 验收
架构文档经过 V1→V2→V3→V3.1 四轮迭代，三层边界在代码落地过程中多次验证，所有后续组件严格在各自层内实现。

---

## P1.2 VS2022 + Qt6 + CMake 工程骨架

### 大白话说明
建立 Core、Tests、CLI 等构建目标，支持 VS2022 Preset 一键编译。

### 成果

- 根 `CMakeLists.txt` + `CMakePresets.json`
  - 支持 2 套 Preset：`vs2022-qt6`（引擎独立）、`vs2022-qt6-all`（引擎 + UI 联合）
  - `PICOATE_BUILD_UI=ON` 时额外拉入 `Qt6::Widgets` 和 UI 子目录
  - CMake 3.24+、C++20、AUTOMOC 自动处理 Qt Meta-Object

- 工程结构（`src/` 目录树）
  ```
  src/
  ├── CMakeLists.txt          # add_subdirectory(core + cli + mockhost + ...)
  ├── core/CMakeLists.txt     # PicoATECore STATIC 库，只 link Qt6::Core
  ├── cli/CMakeLists.txt      # PicoATE.Cli.exe，link PicoATECore + Qt6::Core
  ├── mockhost/               # 独立 mock 进程（测试用）
  ├── nativehost/             # DLL 子进程宿主（生产级隔离）
  ├── fakeinstrumenthost/     # Persistent Instrument Host（长驻设备进程）
  ├── testdllmodule/          # 测试用 DLL（验证 C ABI）
  └── canexamplemodule/       # 模拟 CAN 解码 DLL
  ```

- UI 工程（`ui/` 目录，独立 VS2022 方案）
  ```
  ui/
  ├── CMakeLists.txt          # 独立 project PicoATE.UI
  ├── CMakePresets.json       # vs2022-qt6 preset
  ├── src/CMakeLists.txt      # PicoATEUiRuntime STATIC + PicoATEUi WIN32 可执行
  └── tests/CMakeLists.txt    # PicoATEUiTests (guileless, 不依赖 Widgets)
  ```

- 构建命令
  ```powershell
  # 引擎独立
  cmake --preset vs2022-qt6 && cmake --build --preset vs2022-qt6-debug
  # UI 独立
  cd ui && cmake --preset vs2022-qt6 && cmake --build --preset vs2022-qt6-debug
  # 引擎 + UI 联合
  cmake --preset vs2022-qt6-all && cmake --build --preset vs2022-qt6-all-debug
  ```

- 三套 VS2022 解决方案
  - `out/build/vs2022-qt6/PicoATE.sln` — 引擎独立方案（启动项 PicoATECli）
  - `ui/out/build/vs2022-qt6/PicoATE.UI.sln` — UI 独立方案（启动项 PicoATEUi）
  - `out/build/vs2022-qt6-all/PicoATE.All.sln` — 联合方案（启动项 PicoATEUi）

### 验收
三套方案均可独立编译，Debug 构建通过。`PicoATECore` 不 link `Qt6::Widgets`，CLI 和 UI 平行存在共用同一个 Core。

---

## P1.3 公共类型与不可变 ExecutionPlan

### 大白话说明
定义节点、边、CleanupRegion、Frame、Activation、Attempt 等核心值类型。ExecutionPlan 是编译器产出的不可变模板，Scheduler 只读不写。

### 关键类型（`src/core/include/PicoATE/Core/ExecutionPlan.h`）

```cpp
// 编译期类型（不可变）
struct ExecNode {              // 执行节点
    NodeId id;                 // 唯一标识
    ExecNodeKind kind;         // Noop / Wait / Action / Barrier / Cleanup / Loop
    QVector<ResourceRequirement> resources;  // 所需仪器资源
    RetryPolicy retry;         // 重试次数、间隔
    NodeErrorPolicy errorPolicy; // onFail/onError/onTimeout 三路 ErrorAction
    bool alwaysRun;            // Cleanup 语义：即使前置失败也执行
    ...
};

struct ExecEdge {              // 执行边
    EdgeKind kind;             // Dependency / Control / Finally / BarrierJoin
    EdgeTrigger trigger;       // OnSuccess / OnFail / OnError / OnTimeout / Finally / Always
    ...
};

struct CleanupRegion {         // 清理区域（编译器静态生成）
    QVector<NodeId> entryNodes;
    QVector<CleanupReason> triggers;  // NormalCompletion / StepFailed / UserStop / ...
};

struct ForLoopSpec {           // 循环规格（不可变）
    QString variableName;
    int from, to, step;
};

struct LoopRegion {            // 循环区域（不可变）
    NodeId controllerNodeId;   // LoopController 节点
    QVector<NodeId> bodyNodes; // 循环体内的节点
    ForLoopSpec forLoop;       // 循环规格
};

struct ExecutionPlan {         // 不可变执行模板
    QHash<NodeId, ExecNode> nodes;
    QVector<ExecEdge> edges;
    QVector<CleanupRegion> cleanupRegions;
    QVector<LoopRegion> loopRegions;
    NodeId entryNodeId, exitNodeId;
};
```

### 运行期类型（`src/core/include/PicoATE/Core/RuntimeTypes.h`）

```cpp
// 运行期类型（可变）
struct ExecutionFrame {        // Plan 在运行期的一次激活（SequenceCall/Loop/Batch 创建）
    FrameId id, parentFrameId;
    FrameKind kind;            // RootSequence / SequenceCall / LoopIteration / Cleanup / ...
    int loopIndex;             // Loop 迭代索引
};

struct NodeActivation {        // 节点在某个 Frame 中的一次激活
    QVector<NodeAttempt> attempts;  // 每次 Retry 产生一个新 Attempt
    ActivationState state;     // Created → Ready → Running → Passed/Failed/Error/...
};

struct NodeAttempt {           // 某次具体尝试
    int attemptIndex;
    NodeResult result;         // Passed / Failed / Error / Timeout
    ResourceLeaseId leaseId;   // 当前 Attempt 持有的资源
};

struct UutExecution {          // 单个 UUT 的运行状态
    UutId uutId;
    QHash<NodeId, NodeActivation> activations;  // 所有节点的激活状态
    QVariantMap variables;                      // 运行期变量（Loop 变量、station 变量）
};
```

### 状态机

```
ExecutionState:
  Idle → Starting → Running → Paused / Stopping / Completed / Aborted

ActivationState (per-node):
  Created → WaitingForDependency → WaitingForResource → WaitingAtBarrier
  → Ready → Running → Passed / Failed / Error / Timeout / Cancelled / Skipped
```

### 验收
`ExecutionPlan` 在编译后不可变，Scheduler 只读。Loop、Retry、SequenceCall 不修改 Plan，通过 Frame/Activation/Attempt 表达动态行为。13 个 CTest 覆盖类型引用完整性。
