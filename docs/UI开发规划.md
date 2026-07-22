# PicoATE UI 开发规划

版本：V1.3
日期：2026-07-04
技术栈：Visual Studio 2022、C++20、Qt 6 Widgets、CMake

## 1. 规划目标

PicoATE UI 的第一目标不是立即复制 TestStand 的全部编辑能力，而是先形成一个
稳定、可运行、可观察的测试执行端。Runner 稳定后，再增加 Sequence Editor、
Station Editor 和调试功能。

UI 必须遵守 PicoATE 的三层解耦目标：

```text
Qt Widgets
    ↓ 命令、Signal、只读 DTO
ExecutionViewModel / UI Models
    ↓ 异步执行服务
PicoATECore
    ↓
业务模块、设备 Session、NativeHost / Python / 外部 EXE
```

项目交付时，更换测试流程、测试参数或底层业务模块，不应要求修改 UI 和任务调度
框架。UI 只负责配置、发出命令、展示状态和消费报告。

## 2. 计划基线

本计划与《PicoATE 任务引擎研发计划》衔接：任务引擎计划基线结束日期为
2026-10-20，UI 正式功能开发从 2026-10-21 开始。

| 项目 | 规划值 |
|------|--------|
| UI 正式功能计划开始 | 2026-10-21 |
| UI 计划结束 | 2027-02-23 |
| 估算工时 | 716 小时 |
| 估算人日 | 89.5 人日 |
| 人力口径 | 1 名熟悉 C++、Qt Widgets、CMake 的工程师 |
| 工作日口径 | 8 小时/人日，周一至周五，暂不扣除法定节假日 |
| 前置工作 | Engine/UI/All 三套 VS2022 方案和 Runner 外壳 |

法定节假日、需求评审等待、真实硬件等待、UI 视觉专项评审不包含在上述日期中，
正式项目排期时需要按公司日历重新校准。

## 3. 范围边界

### 3.1 首轮包含

- Sequence 和 Station JSON 文件加载；
- 编译错误、Warning 和建议展示；
- 多 UUT 运行、停止和最终结果展示；
- Step、Attempt、Loop iteration、Measurement、Limit 展示；
- 运行时进度、设备状态、Host 状态和诊断日志；
- 结果历史、筛选和 JSON/CSV 导出；
- Sequence 可视化编辑；
- Station 和逻辑设备配置编辑；
- 在引擎能力具备后接入暂停、继续、断点和变量查看；
- 设置、布局保存、部署打包和发布验收。

### 3.2 首轮不包含

- 自研图形渲染框架；
- Web 前端或浏览器版本；
- 任意厂商仪器面板的专用 UI；
- 在 UI 中直接加载和调用厂商 DLL；
- 绕过 SequenceCompiler 手工拼装 ExecutionPlan；
- UI 直接修改 Scheduler ready queue、UutExecution 或 NodeActivation；
- 未经引擎支持就在 UI 层模拟 Pause/Resume 或 Checkpoint 恢复。

## 4. 工程与依赖边界

当前提供三套 Visual Studio 解决方案：

| 解决方案 | 路径 | 用途 |
|----------|------|------|
| Engine | `out/build/vs2022-qt6/PicoATE.sln` | 独立开发 Core、CLI、Host 和测试 |
| UI | `ui/out/build/vs2022-qt6/PicoATE.UI.sln` | 独立开发 Qt Runner，按源码依赖 PicoATECore |
| All | `out/build/vs2022-qt6-all/PicoATE.All.sln` | 一起编译 Engine 和 UI |

依赖规则：

1. `PicoATECore` 不链接 Qt Widgets。
2. `PicoATEUi` 可以链接 `PicoATECore`、Qt Core 和 Qt Widgets。
3. Widgets 不保存 Core 可变对象的裸指针或引用。
4. Core 向 UI 传递值类型 DTO 或不可变快照。
5. UI 的业务命令经过 ViewModel 和执行服务，不直接调用 Scheduler 内部方法。
6. CLI 与 UI 平行存在，共用同一个 Core，不互相依赖。

## 5. UI 分层

```text
ui/
├── app/                    # QApplication、主窗口、菜单和窗口生命周期
├── viewmodels/             # 命令状态、页面状态、DTO 到 UI Model 的转换
├── models/                 # QAbstractItemModel：UUT、Step、Measurement、Diagnostic
├── services/               # 异步编译/执行、文件、报告历史、设置
├── widgets/                # 可复用操作型控件
├── views/                  # Runner、Editor、Station、History 页面
└── tests/                  # ViewModel、Model、线程和 UI 冒烟测试
```

建议的主要对象：

| 对象 | 职责 | 明确不负责 |
|------|------|------------|
| `ExecutionViewModel` | 组合编译、运行、停止命令和页面状态 | 不执行 Scheduler pump |
| `IExecutionService` | 隔离 UI 与 Core 执行入口，便于 Fake 测试 | 不保存 Widget 指针 |
| `ExecutionWorker` | 在工作线程创建并持有 ExecutionSession | 不操作界面控件 |
| `DiagnosticModel` | 展示编译错误、Warning 和运行诊断 | 不重新解析 JSON |
| `UutResultModel` | 展示 UUT、Step 和状态层级 | 不读取可变 Activation |
| `MeasurementModel` | 展示测量值、单位、限值和判定 | 不重新计算业务限值 |
| `DeviceStatusModel` | 展示逻辑设备和 Host 状态 | 不直接持有厂商 Driver |
| `SequenceDocument` | Sequence JSON 文档、dirty 状态、不可变快照与原子保存 | 不直接生成调度节点 |
| `StationDocument` | Station JSON 编辑和校验 | 不直接连接硬件 |

## 6. ViewModel 状态机

UI 不直接复用单一的 `ExecutionState` 作为全部页面状态，因为编译前、文件错误和
Station 校验属于 UI 工作流，不属于运行时状态。

```text
Empty
  ↓ select sequence
SourceSelected
  ↓ compile
Compiling
  ├── error ──> CompileFailed
  └── success -> Ready
                  ↓ run
                Starting
                  ↓
                Running
                  ├── stop -> Stopping -> Completed / Failed
                  └── finish ---------> Completed / Failed
```

建议的 UI 状态：

```cpp
enum class UiRunState {
    Empty,
    SourceSelected,
    Compiling,
    CompileFailed,
    Ready,
    Starting,
    Running,
    Stopping,
    Completed,
    Failed
};
```

按钮启用规则由 ViewModel 统一计算：

| 命令 | 可用状态 |
|------|----------|
| Open Sequence | 除 Starting/Stopping 外 |
| Open Station | 除 Starting/Stopping 外 |
| Compile | SourceSelected、CompileFailed、Ready、Completed、Failed |
| Run | Ready、Completed、Failed，且编译输入未变更 |
| Stop | Running |
| Export Report | Completed、Failed，且存在 ExecutionReport |

## 7. 线程模型

### 7.1 所有权

```text
Qt 主线程
  MainWindow
  ExecutionViewModel
  QAbstractItemModel
        │ queued signal / immutable DTO
        ▼
执行工作线程
  ExecutionWorker
  SequenceCompiler
  ExecutionSession
  DeviceSessionManager
  Module / Transport 调用
```

`ExecutionSession`、`StationRuntime` 相关对象在工作线程内创建、使用并销毁，不能
先在主线程创建后随意跨线程调用。

### 7.2 Stop 风险与处理

`ExecutionSession::run()` 是同步阻塞调用，因此 UI 主线程不能直接修改正在工作线程
运行的 Session。UI-1 已将停止请求从 Session 普通成员中拆出，改为共享的线程安全
`StopToken`。

UI-1 评审时确定了以下两条实现路径：

1. 引擎引入线程安全 `StopController/StopToken`，内部使用原子状态；或
2. 将 Scheduler 改为可由事件循环分步 pump，并在同一线程处理 stop 命令。

当前已完成第一条路径：`StopToken` 使用单个原子优先级状态，Graceful 请求可以升级
为 Abort，但不会被较弱请求降级；`ExecutionSession` 只在运行线程消费停止状态。
所有 ViewModel 状态更新通过 Qt queued signal 返回主线程。分步 pump 保留为未来实时
调试能力的可选演进方向。

### 7.3 数据传递

- 编译结果使用值类型 Diagnostic DTO；
- 最终结果使用 `ExecutionReport` 副本；
- 实时状态使用轻量不可变 Event DTO；
- 高频事件必须合并或限频，建议 UI 刷新频率不超过 10-20 Hz；
- 不把 `UutExecution*`、`NodeActivation*`、`ExecutionSession*` 暴露给 Widget。

UI-3 已按上述边界实现：Core 通过 `IRuntimeEventSink` 输出只读 `RuntimeEvent`，
Worker 线程写入 `BufferedRuntimeEventSink`，Qt 主线程每 50ms（20Hz）批量取出事件并
更新模型。事件设置 20000 条缓冲上限，最终状态始终用 `ExecutionReport` 覆盖校准，
实时事件丢失或合并不能改变最终测试结果。

### 7.4 Runtime Event 与业务日志边界

UI-3 的 Runtime Event 表示 Session、UUT、Node、Attempt、Retry、Loop、Barrier、
Cleanup 和 Device 状态变化。业务 DLL 日志使用可选 `PicoATE_SetLogSink` callback，经过
NativeHost/Transport 转换为 `ModuleLog` 事件实时进入 CLI/UI；最终测试判定仍以
`ExecutionReport` 为准。Host 厂商 stdout/stderr 已隔离为有界 vendor 日志，不能直接
混入 JSON 协议流。

## 8. 页面规划

### 8.1 Runner 主页面

第一版主页面采用操作台布局，不做营销式首页：

```text
┌─────────────────────────────────────────────────────────────┐
│ Open Sequence | Open Station | Compile | Run | Stop        │
├──────────────────┬──────────────────────┬───────────────────┤
│ UUT / Step Tree  │ Step / Attempt Detail│ Measurements      │
│                  │                      │ Limits / Outputs  │
├──────────────────┴──────────────────────┴───────────────────┤
│ Diagnostics / Runtime Log / Device Status                  │
└─────────────────────────────────────────────────────────────┘
```

TEST 模式当前采用单 UUT 专用视图：不显示 UUT 根节点，直接按
`SETUP / MAIN / CLEANUP` 分段展开完整流程。该分段由 Core 的只读阶段字段驱动，
Admin 运行页继续保留原有 UUT/Step 调试树。

多 UUT TEST 视图不在当前实现范围。后续确认产线确有并行工位需求时，计划把测试区
拆成最多四个 UUT 面板，按上二下二的 2×2 方式排列；每个面板独立显示 SN、阶段、
流程状态和结果。当前不提前增加空面板、切换开关或多 UUT 页面复杂度。

Admin 工作台当前使用四个一级分页：`Run Test / Flow Editor / Station Config / Reports`。
登录后固定先显示 `Run Test`，其单 UUT 信息、结果颜色、阶段分区、实时进度和良率布局
与 TEST 保持一致；顶部工具栏增加编译、断点单步和按需扫码命令。Admin 不自动弹扫码
框，`Scan SN` 只负责主动发起一轮带真实 SN 的运行。

### 8.2 Sequence Editor

- 左侧 Group/Step 树；
- 中间流程顺序和启用状态；
- 右侧属性编辑器；
- 编译错误可点击定位到字段；
- 支持添加、删除、复制、移动、撤销和重做；
- 保存前通过 SequenceCompiler 校验；
- 保留未知 `x-*` 和 `vendor` 扩展字段，避免编辑后丢数据。

### 8.3 Station Editor

- 逻辑设备列表：`DMM1`、`CAN1`、`PSU1`；
- driverId、address、lifetime、options 编辑；
- 环境变量和路径变量预览；
- 配置校验；
- 在明确安全边界后提供独立的连接测试命令。

### 8.4 History / Report

- 按日期、Sequence、Station、UUT、结果筛选；
- 展开 Step、Attempt、Loop iteration 和 Measurement；
- 导出 JSON/CSV；
- 报告展示使用保存时的 DTO，不依赖当前 Sequence 文件仍然存在。

## 9. 阶段计划

| 阶段 | 计划日期 | 工时 | 人日 | 目标 | 主要交付物 | 状态 |
|------|----------|------|------|------|------------|------|
| UI-0 工程准备 | 前置 | 16h | 2 | 独立 Engine/UI/All 方案和 Runner 外壳 | 三套 `.sln`、基础 MainWindow | 已具备 |
| UI-1 ViewModel 与线程边界 | 2026-10-21 ~ 2026-11-04 | 84h | 10.5 | 建立安全的编译、运行、停止入口 | ExecutionViewModel、IExecutionService、Worker、StopToken | 已完成 |
| UI-2 最小可用 Runner | 2026-11-05 ~ 2026-11-16 | 64h | 8 | 从文件到最终报告完整跑通 | Compile/Run/Stop、UUT/Step/Attempt/Measurement | 已完成 |
| UI-3 实时运行监控 | 2026-11-17 ~ 2026-11-30 | 80h | 10 | 运行过程中观察进度和设备状态 | Event DTO、实时模型、设备状态；Host 诊断独立增强 | 已完成 |
| UI-4 报告与历史 | 2026-12-01 ~ 2026-12-10 | 64h | 8 | 保存、查询和导出测试结果 | Report Serializer、History、CSV/JSON Export | 已完成 |
| UI-5 Sequence Editor | 2026-12-11 ~ 2027-01-07 | 160h | 20 | 可视化编辑测试流程 | 文档模型、流程树、属性面板、Undo/Redo | 已完成 |
| UI-6 Station Editor | 2027-01-08 ~ 2027-01-19 | 64h | 8 | 配置逻辑设备和工位信息 | Station 文档、设备表、属性和校验 | 已完成 |
| UI-7 调试工具 | 2027-01-20 ~ 2027-02-09 | 120h | 15 | 接入暂停、继续、断点和运行时观察 | Debug 控制、变量、资源、Barrier、Timeline | 进行中 |
| UI-8 产品化与发布 | 2027-02-10 ~ 2027-02-23 | 80h | 10 | 形成可部署、可恢复的桌面程序 | 设置、日志、部署包、发布验收 | 计划 |

UI-0 为已经建立的前置工程能力，不计入 716 小时正式功能基线。包含 UI-0 时总工作量
为 732 小时，即 91.5 人日。

## 10. 详细 WBS

### UI-1：ViewModel 与线程边界

| WBS | 工作项 | 工时 | 前置 | 验收标准 |
|-----|--------|------|------|----------|
| UI-1.1 | 定义 UiRunState、命令可用性和错误状态 | 12h | UI-0 | 状态转换和按钮规则有 QtTest |
| UI-1.2 | 定义 `IExecutionService` 和 UI DTO | 12h | UI-1.1 | ViewModel 测试可使用 Fake Service |
| UI-1.3 | Sequence/Station 文件加载与异步编译 | 16h | UI-1.2 | 编译错误、Warning、建议可稳定返回主线程 |
| UI-1.4 | Worker 线程和 ExecutionSession 生命周期 | 20h | UI-1.2 | Session 全生命周期位于 Worker 线程 |
| UI-1.5 | StopToken 和线程安全停止语义 | 16h | UI-1.4 | 连续 Stop、完成瞬间 Stop 无数据竞争或死锁 |
| UI-1.6 | ViewModel/线程自动化测试 | 8h | UI-1.3~1.5 | 正常、编译失败、运行失败、停止均覆盖 |

### UI-2：最小可用 Runner

| WBS | 工作项 | 工时 | 前置 | 验收标准 |
|-----|--------|------|------|----------|
| UI-2.1 | Sequence/Station 来源区与最近路径 | 8h | UI-1 | 文件变更后旧编译结果自动失效 |
| UI-2.2 | Compile Diagnostic Model 和定位 | 12h | UI-1.3 | Error/Warning 分开展示，可复制路径和建议 |
| UI-2.3 | UUT/Step 层级结果模型 | 16h | UI-1.4 | 顺序与 ExecutionReport 一致，不读 Activation |
| UI-2.4 | Attempt/Loop/Measurement 详情 | 12h | UI-2.3 | 重试、循环、单位、限值和判定完整展示 |
| UI-2.5 | Compile/Run/Stop 命令栏 | 8h | UI-1.1 | 命令启用状态符合状态机 |
| UI-2.6 | Runner 冒烟与失败路径测试 | 8h | UI-2.1~2.5 | simple/basic/dmm-can 示例可从 UI 跑通 |

### UI-3：实时运行监控

| WBS | 工作项 | 工时 | 前置 | 验收标准 |
|-----|--------|------|------|----------|
| UI-3.1 | 定义 Runtime Event DTO | 20h | UI-2 | Node/UUT/Session 事件不暴露可变 Core 对象 |
| UI-3.2 | Core Observer 到 Qt Signal Bridge | 20h | UI-3.1 | 跨线程 queued delivery，无直接 Widget 回调 |
| UI-3.3 | 实时 UUT/Step 状态更新 | 16h | UI-3.2 | 多 UUT 运行时状态正确、不乱序 |
| UI-3.4 | Device/Host 状态模型 | 12h | UI-3.2 | 显示 logical id、连接、health、错误摘要 |
| UI-3.5 | 事件合并、限频和背压 | 12h | UI-3.3 | 高频节点执行时 UI 保持响应且最终状态不丢失 |

UI-3 实际完成项：

- `RuntimeEvent`、`IRuntimeEventSink` 和单 Session 单调序号发射器；
- Session/UUT/Node/Attempt/Retry/Loop/Barrier/Stop/Cleanup 事件；
- Device 配置、连接中、连接成功、复用、断开和错误事件；
- 线程安全缓冲、50ms 批处理、最大缓冲数量和主线程 Model 更新；
- UUT/Step/Attempt/Measurement 实时刷新和 Device 状态页；
- 结束时由最终 `ExecutionReport` 覆盖实时模型；
- 双 UUT Barrier、Loop、Retry、Stop/Cleanup、事件顺序和模型测试。

### UI-4：报告与历史

| WBS | 工作项 | 工时 | 前置 | 验收标准 |
|-----|--------|------|------|----------|
| UI-4.1 | ExecutionReport JSON 序列化 | 16h | UI-2 | 保存后可独立加载，字段版本明确 |
| UI-4.2 | 本地运行历史索引 | 16h | UI-4.1 | 支持按时间、Sequence、UUT 和结果查询 |
| UI-4.3 | Report Viewer 和筛选 | 16h | UI-4.2 | Step/Attempt/Measurement 可展开和过滤 |
| UI-4.4 | JSON/CSV 导出 | 8h | UI-4.1 | 中文、单位和错误文本导出不乱码 |
| UI-4.5 | 报告兼容与损坏文件测试 | 8h | UI-4.1~4.4 | 旧版本、缺字段、损坏文件有明确诊断 |

UI-4 实际完成项：

- V1 `picoate.execution-report` JSON 格式，完整保存 UUT/Step/Attempt/Loop/Measurement；
- 未知 schema version 明确拒绝，缺少 V1 可选字段时兼容默认值；
- 最终报告自动保存，历史索引使用原子写入，索引损坏时可扫描报告重建；
- History Model 支持按 Sequence、UUT、状态和结果文本筛选；
- History 页支持双击加载、JSON 导出和 CSV 导出；
- CSV 使用 UTF-8 BOM，覆盖中文、逗号、双引号、Limit 和错误字段；
- 历史报告加载后仍使用现有 UUT/Step/Attempt/Measurement 模型展示。

### UI-5：Sequence Editor

| WBS | 工作项 | 工时 | 前置 | 验收标准 |
|-----|--------|------|------|----------|
| UI-5.1 | SequenceDocument 与 JSON 无损往返 | 24h | UI-2 | 已知字段和扩展字段保存后不丢失 |
| UI-5.2 | Group/Step 树模型 | 32h | UI-5.1 | Setup/Main/Custom/Cleanup 正确表达 |
| UI-5.3 | Step 属性编辑器 | 40h | UI-5.2 | Resource/Retry/Timeout/Error/Barrier/Loop 可编辑 |
| UI-5.4 | 添加、删除、复制、移动和拖放 | 24h | UI-5.2 | 操作后 ID 和组语义保持有效 |
| UI-5.5 | Undo/Redo、Dirty 和保存保护 | 24h | UI-5.4 | 关闭未保存文档有保护，撤销链稳定 |
| UI-5.6 | 编译校验和字段定位 | 16h | UI-5.3 | 点击诊断能定位对应 Step 和属性 |

UI-5 当前完成范围（2026-07-04）：

- `SequenceDocument` 支持 JSON 加载、编译诊断、dirty 状态、未保存关闭保护和 `QSaveFile` 原子保存；
- 文档以原始 `QJsonObject` 为所有权边界，编辑/保存时保留未知字段、`x-*` 和 `vendor` 扩展内容；
- 每次编辑生成新 revision，不可变 JSON 快照交给 `ExecutionViewModel`，执行线程不读取正在变化的编辑对象；
- `SequenceTreeModel` 递归展示 Group、TestItem、Loop 及普通 Step，支持 Enabled 勾选；
- 已完成新增、删除、复制、上移、下移，并按顶层 `001`、子层 `01` 规则生成局部 ID；
- `StepPropertyEditor` 提供 General、Data、Policies 三页，按 Group/Action/Limit/Loop/Barrier/TestItem 等类型切换字段；
- 支持 Module/Function、Inputs/Parameters JSON、Retry、Timeout、ErrorPolicy、Resource、Loop 和 Barrier 配置；
- 属性修改以源 JSON 对象为草稿，一次 Apply 只产生一个 revision，并保留对象及嵌套策略中的扩展字段；
- Inputs、Parameters 和 Resources 在提交前进行 JSON 类型/语法校验，失败时不会污染文档；
- `SequenceDocument` 使用最多 200 步的 `QUndoStack`，属性 Apply、Enabled、增删复制和移动共享同一撤销链；
- 保存成功建立 clean point，Undo 回到保存点自动清除 dirty，Redo 离开保存点重新标记 dirty；
- 支持同一父节点内拖拽排序；跨 Group、跨 TestItem/Loop 的拖拽被模型明确拒绝；
- Undo/Redo 后按同父级 `key/id` 恢复选择，避免移动后选中同一行的另一个 Step；
- 编译诊断路径可解析 Group、任意层嵌套 Step 和数组字段，点击后选择并滚动到最近可用节点；
- 属性面板根据字段路径自动切换 General/Data/Policies，聚焦并短暂高亮对应控件；
- Wait Step 新增 `Duration` 控件，覆盖高频顶层 `ms` 快捷字段及其诊断定位；
- 编译 Error/Warning 在编辑页实时刷新；文件加载失败不会覆盖当前已打开文档；
- 已增加 unknown 字段往返、快照隔离、原子对象替换、Undo/Redo clean point、拖拽边界、嵌套树编辑和内存快照编译测试。

UI-5 已完成全部 WBS。Sequence 根级和 `moduleBindings` 诊断当前只显示状态提示，
因为它们不属于 Group/Step 属性面板；后续如增加 Sequence Settings 页面，可复用同一
诊断路径入口继续定位。

### UI-6：Station Editor

| WBS | 工作项 | 工时 | 前置 | 验收标准 |
|-----|--------|------|------|----------|
| UI-6.1 | StationDocument 和变量保留 | 12h | UI-4 | station JSON 往返不破坏变量表达式 |
| UI-6.2 | 逻辑设备列表模型 | 16h | UI-6.1 | 增删 DMM/CAN/PSU 并检测重复 ID |
| UI-6.3 | Driver/Address/Lifetime/Options 编辑 | 16h | UI-6.2 | 字段类型和枚举实时校验 |
| UI-6.4 | 安全的连接测试命令 | 12h | UI-3.4 | 连接测试独立运行、可超时、总能清理 |
| UI-6.5 | 保存和错误路径测试 | 8h | UI-6.1~6.4 | 错误路径精确，不覆盖原文件 |

UI-6 当前完成范围（2026-07-05）：

- 新增 `StationDocument`，支持 JSON 无损往返、revision 快照、dirty、最多 200 步 Undo/Redo、`QSaveFile` 原子保存和关闭未保存保护；
- 根对象和 Device 编辑均从原始 `QJsonObject` 派生，保留 unknown、`x-*`、`vendor` 及变量表达式；
- 新增 `StationDeviceModel`，展示 Logical ID、Type、Driver、Address、Lifetime 和 Enabled；
- 支持设备新增、删除、复制、上移、下移和 Enabled 勾选，并通过 Core StationConfig 校验重复 ID、字段类型和 lifetime；
- 新增 Station/Device 属性页，支持 stationId、name、metadata、driverId、address、lifetime 和 options 编辑；
- Station 诊断可定位设备行和属性字段；Sequence 与 Station 均以不可变 JSON 快照进入异步 Compile；
- `CompileRequest::stationJson` 已打通，未保存的 Station 修改不会被磁盘旧文件覆盖；
- UI 支持第二个命令行参数直接打开 Station 文件，便于 VS 调试和自动验收；
- 文档、模型、属性编辑、保存/不保存、快照优先级和窗口生命周期测试通过；Debug 全套测试 16/16 通过。

UI-6.4/6.5 完成范围（2026-07-05）：

- 新增连接测试请求/结果 DTO，结果明确区分 Passed、Failed、TimedOut 和 Cancelled；
- 连接测试在 `ExecutionWorker` 后台线程执行，UI 和 StationDocument 不持有 Driver、Transport 或 DeviceSession；
- 测试流程固定为 open -> health -> close，成功、失败、取消和超时路径都会进入 close 清理；
- Sequence `moduleBindings` 除注册业务模块外，也按同名 ID 注册共享 Transport 的 `DeviceSessionFactory`；Station `driverId` 与 binding `moduleId` 一致时即可复用同一 Transport；
- Station 工具栏新增 Timeout 和 Test Connection；设备表新增 Connection 列，实时显示 Testing 与最终结果；
- disabled 设备不能触发连接测试，运行或连接测试期间 Station 编辑统一禁用，顶部 Stop 可请求取消；
- 覆盖 fake host 成功、缺 Driver、Host 延迟超时、Host 直接退出、StopToken 取消和对象级 UI Action 触发；
- `dmm_can_adapter_sequence.json` 已补 `fake.dmm/fake.can` bindings，可直接配合 basic station 验证。

取消语义为协作式：如果 Transport 正在进行阻塞调用，取消会在当前调用达到配置的 timeout
后生效并执行清理，不使用不安全的线程强杀。UI-6 全部 WBS 已完成。

### UI-7：调试工具

| WBS | 工作项 | 工时 | 前置 | 验收标准 |
|-----|--------|------|------|----------|
| UI-7.1 | 引擎 Pause/Resume/Breakpoint 契约 | 32h | Core 支持 | 调度安全点语义明确且有 Core 测试 |
| UI-7.2 | Debug 命令和状态机 | 16h | UI-7.1 | 非法状态命令不可触发 |
| UI-7.3 | 变量和作用域查看 | 20h | UI-7.1 | UUT/Frame/Loop/Attempt 作用域可区分 |
| UI-7.4 | Resource/Barrier/Loop 观察器 | 20h | UI-3 | 只读展示，不允许 UI 修改队列 |
| UI-7.5 | 运行时间线 | 16h | UI-3.1 | Node/Attempt/Loop 事件顺序可追踪 |
| UI-7.6 | 调试回归测试 | 16h | UI-7.1~7.5 | 暂停、停止、异常、Cleanup 组合测试通过 |

### UI-8：产品化与发布

| WBS | 工作项 | 工时 | 前置 | 验收标准 |
|-----|--------|------|------|----------|
| UI-8.1 | 设置、最近文件和布局持久化 | 12h | UI-2 | 异常布局可恢复默认值 |
| UI-8.2 | 统一样式、可访问性和中英文准备 | 12h | 全部页面 | 字体、缩放、键盘焦点和长文本无重叠 |
| UI-8.3 | 日志、崩溃信息和恢复提示 | 16h | UI-3/4 | 错误可定位，日志不泄漏敏感参数 |
| UI-8.4 | Qt Runtime 部署和安装包 | 20h | 功能冻结 | 干净机器可安装、启动和卸载 |
| UI-8.5 | 性能与可用性测试 | 12h | 功能冻结 | 长序列、多 UUT 下界面保持响应 |
| UI-8.6 | 发布验收清单 | 8h | UI-8.1~8.5 | 构建、测试、文档和样例全部可复现 |

## 11. 测试策略

### 11.1 ViewModel 测试

- 使用 Fake `IExecutionService`，不启动真实 Session；
- 验证所有 UiRunState 转换；
- 验证命令启用状态；
- 验证过期编译结果不会被运行；
- 验证 Stop 幂等；
- 验证 Worker 销毁后不会向已销毁 ViewModel 发信号。

### 11.2 Model 测试

- `ExecutionReport` 到树模型的排序；
- Retry Attempt、Loop iteration、Measurement 和 Limit 映射；
- 多 UUT 增量更新；
- 大量 Step 下的更新性能；
- 行删除、重置和筛选期间 QModelIndex 有效性。

### 11.3 集成测试

- UI -> Compile -> Session -> ExecutionReport；
- UI -> Station -> DeviceSession -> Fake Instrument Host；
- 编译失败、模块不存在、Host 超时、用户 Stop、Cleanup；
- UI 关闭时仍有任务运行；
- Host 崩溃后 UI 不崩溃并给出可读诊断。

### 11.4 冒烟测试

- 独立 `PicoATE.UI.sln` 编译；
- 联合 `PicoATE.All.sln` 编译；
- 应用启动和主窗口创建；
- 打开 Sequence/Station；
- simple、basic、for-loop、dmm-can 示例运行；
- 结果导出并重新加载。

## 12. 风险与应对

| 风险 | 影响 | 应对 |
|------|------|------|
| `requestStop()` 跨线程不安全 | 数据竞争、偶发崩溃 | 已通过原子 StopToken 和 Worker 独占 Session 解决 |
| Core 缺少实时事件 | UI 只能运行结束后刷新 | 已通过只读 RuntimeEvent/IRuntimeEventSink 解决 |
| 高频事件淹没 UI 线程 | 界面卡顿、内存增长 | 合并、限频、只保留最新状态和关键事件 |
| Sequence Editor 保存丢扩展字段 | 项目配置被破坏 | 文档模型保留 unknown/x-*/vendor 原始字段 |
| UI 和 CLI 行为不一致 | 同一流程结果不同 | 两者共用 SequenceCompiler、Session 和 Report DTO |
| 暂停/断点只在 UI 模拟 | 调度状态失真 | Pause/Resume 已落到 Core；Breakpoint 继续沿用同一安全点契约 |
| 真实硬件不可用 | Device 页面无法完整验收 | 先使用 Fake Host，真实设备单独安排联调 |
| UI 独立方案重复编译 Core | 构建时间增加 | 首轮接受源码依赖，发布阶段增加 install/export SDK |
| Qt 部署遗漏插件 | 开发机能跑、现场机不能跑 | UI-8 使用 windeployqt/CPack 并在干净环境验证 |

## 13. 阶段验收门槛

### MVP Runner（UI-2 完成）

- 能选择 Sequence 和 Station；
- 能显示编译 Error/Warning；
- 能 Run/Stop 且界面不冻结；
- 能显示 UUT、Step、Attempt、Loop、Measurement 和 Limit；
- UI 不读取 Core 可变运行时对象；
- Engine 现有回归测试保持通过。

### 可观测 Runner（UI-4 完成）

- 运行中可观察 UUT、Step 和设备状态；
- Host/Transport 错误有可读诊断；
- 结果可保存、查询和导出；
- 高频事件下 UI 保持响应。

### 可配置工作站（UI-6 完成）

- Sequence 和 Station 均可视化编辑；
- 保存前有类型和语义校验；
- JSON 扩展字段不会被编辑器吞掉；
- 无需修改 UI/调度代码即可增加项目流程和逻辑设备配置。

### 发布候选（UI-8 完成）

- 安装包可在干净 Windows 环境运行；
- 崩溃、超时和配置错误有可追踪日志；
- 长序列、多 UUT、重复运行通过稳定性验证；
- 用户文档、开发文档、示例和验收清单齐全。

## 14. 下一步

UI-1 已完成以下内容：

1. `UiRunState`、请求 ID 和命令可用性；
2. `IExecutionService`、`CoreExecutionService` 和 `ExecutionWorker`；
3. 工作线程独占 Session，主线程通过 `StopToken` 停止；
4. Fake Service 状态机测试和真实 simple sequence 集成测试；
5. MainWindow 的 Open、Compile、Run、Stop、Diagnostic 和基础结果绑定。

UI-2 已完成以下内容：

1. `DiagnosticModel`、`UutStepModel`、`AttemptModel` 和 `MeasurementModel`；
2. UUT/Step -> Attempt -> Measurement 的只读选择联动；
3. UUT 数量输入和多 UUT Run 请求；
4. basic sequence 的 2 UUT Barrier/Measurement 验证；
5. for-loop sequence 的 3 次 iteration/attempt/measurement 验证；
6. `QAbstractItemModelTester` 模型一致性检查。

UI-3 至 UI-6 已完成。UI-7 当前已完成 Pause/Resume 基础契约、Breakpoint 地址模型、只读
Debug Snapshot、Step Into、Step Over、调试观察器 UI、Breakpoint 编辑 UI、调试时间线和 Timeline 事件联动：Core 使用线程安全 `ExecutionControl`，在节点边界暂停整个
Session；断点命中发生在 Step 执行前，支持内部唯一 `nodePath` 和人可读 `localPath`（如 `001/03`）；
Debug Snapshot 复制 UUT、变量、节点、attempt、资源和 Barrier 状态，不暴露可变 Session 指针。
Step Into 执行一个调度节点后暂停；Step Over 对普通节点等同单步，对 Loop/TestItem 结构节点会跑完整个
结构块后暂停。暂停期间 Stop/Abort、Cleanup、资源释放、Barrier 状态保持、断点恢复防重复命中和
Step Over 跳过结构块内部断点均已有回归测试。Run 页右侧的 Debug tab 可以在暂停后查看当前 UUT、
当前 Step、变量、节点状态、Attempt、outputs、资源租约/等待队列和 Barrier 状态。Sequence 树的 BP
列可以设置临时断点，Run 前同步到 Core，命中后自动定位当前 Step 并显示命中状态。Timeline tab
展示非日志 RuntimeEvent 顺序，用于观察 Node、Attempt、Loop、Barrier、Retry、断点和单步事件；
点击 Timeline 某个 Step 事件会同步选中结果树和 Sequence 树，断点/单步暂停也会自动聚焦对应 Timeline 行。

UI-8.1 已完成：窗口位置与尺寸、五处分栏、工作区/详情页签、UUT 数量和连接超时均可持久化；
File 菜单提供最近 Sequence/Station，View 菜单可以恢复默认布局；损坏或离屏窗口配置会自动回到主屏。

Flow Editor 已补齐插件输出表达式选择：`StepOutputCatalog` 根据插件 Describe outputs、Station 逻辑设备绑定、
当前 Step 位置和引擎 Group 执行顺序生成候选；插件参数及 Limit Actual 均可通过 `fx` 菜单写入完整作用域
表达式，禁用和未来 Step 自动排除，失效引用继续由编译诊断定位。

以上内容是 2026-07-05 时的阶段判断，后续已经优先完成双模式界面、Flow Editor、Station Config、
插件目录和报告输出。当前工作基线以本文第 17 节为准。`UI-TECH-001` 仍作为暂缓遗留项保留。

## 15. 双模式 UI 调整（2026-07-10）

发布打包前先完成 TEST/Admin 使用场景拆分。第一版已经落地日期口令登录、根目录 Sequence 自动发现、
固定 `StationSystem.json`、Station 扫码开关、TEST 专用 ProductionWindow、不可普通关闭的扫码 Dialog、
SN 运行变量、完整流程预览、层级实时结果和状态颜色。详细规则见 `docs/双模式UI设计.md`。

TEST 结果列已经补齐 ErrorCode、Lower、Upper、Actual、Result 和 Time；Step 总耗时与 Attempt 单次耗时
同时进入 RuntimeEvent、ExecutionReport、历史 JSON 和 CSV。下一项是 Admin 工作台重排，随后实现
插件功能目录、拖拽编排和右侧动态参数编辑；布局与数据字段验收后再恢复 UI-8.4 发布工作。

## 16. TEST 产线布局（2026-07-11）

TEST 页面已完成第二版布局，不再沿用通用调试 Runner 的简单上下堆叠结构：

- 顶部依次为当前脚本名称和 Start/Pause/Resume/Stop 控制栏；
- 左侧固定显示 SN、Station、Order、Tester、Jig No.、总结果和总耗时；
- 右侧上方是完整 TestItem/子 Step 结果树，下方是有界实时业务日志；
- 底部进度条显示 Step 执行进度；右侧 PASS、FAIL、TOTAL、YIELD 按整台产品累计良率，不统计 Step 数量；
- 使用中性浅色工业界面，Running/Pass/Fail/Skipped 只在状态结果上使用黄/绿/红/灰；
- 布局保持 TEST 专用，不加入编辑器、断点、设备配置和历史报告页签。

下一项只调整 Admin 工作台的信息架构，不再反复改动 TEST 数据链。

## 17. 2026-07-22 实际进度基线

UI-1 至 UI-7 的主链路已经完成，UI-8 已完成工作区持久化和大部分工程化编辑能力，发布打包仍待收口。

当前已经具备：

- TEST/Admin 登录分流、扫码运行、产线结果页和 Admin 工程工作台；
- Flow Editor 的层级流程编辑、复制粘贴、Undo/Redo、插件拖放、基础功能和类型化参数控件；
- Station Config 的逻辑设备、驱动绑定、通道参数、运行策略、报告路径和连接测试；
- 插件自动扫描、Describe 功能目录、目标设备筛选和 Function 下拉选择；
- 实时运行事件、步骤/Attempt/插件日志、断点单步、结果定位和最终报告对账；
- TXT、CSV、XLSX 报告以及按日期和 PASS/FAIL 分类归档；
- 稳定 Sequence 节点身份、失效索引重定位、无实际修改时保存等于成功等编辑可靠性保护。

当前剩余重点：

1. 在编辑器和编译器增加“功能类别与目标设备类型不匹配”的静态诊断。
2. 使用真实 CAN、DMM、PSU、SCOPE、MODBUS 设备继续做长时间运行和异常恢复验证。
3. 建立 Release 发布目录，收集 Qt Runtime、Engine DLL、NativeHost、插件和默认配置，并在干净电脑验收。
4. 完成登录页、Flow、Station、日志和诊断区域的统一视觉收口；不改变已经稳定的数据链。

2026-07-22 Debug 全量回归基线为 CTest `17/17` 通过。
