# PicoATE Progress Plan

## 2026-06-20 起始总结

项目从架构文档推进到了可编译、可测试的 PicoATE 调度内核 MVP。当前代码已经形成了完整的第一条闭环：

```text
JSON -> SequenceDef -> PlanBuilder -> ExecutionPlan -> ExecutionSession
```

## 2026-06-22 今日总结

今天完成了三块对“配置化交付”非常关键的能力：

| 事项 | 结果 |
|------|------|
| LoopController For 循环 | 固定 `for` 循环进入调度层，业务模块不需要知道循环语义 |
| VariableResolver 中心化 | 统一 `${PROJECT_DIR}`、`${SEQUENCE_DIR}`、显式变量、环境变量和递归替换 |
| NativeHost manifest | DLL 路径、symbol、buffer size、timeout 从命令行参数迁移到 manifest JSON |

今日详细记录见 `docs/work_summary_2026-06-22.md`。

## 2026-06-24 今日总结

今天补上了“没有 CAN 分析仪也能跑”的软件验证路径：新增一个模拟 CAN 解码 DLL，通过 NativeHost manifest 加载，再由 sequence JSON 配置 CAN 报文、信号解析、单位和上下限。这样可以先验证框架边界和项目交付模式，真实硬件驱动等有设备后再替换最底层模块。

| 事项 | 结果 |
|------|------|
| 模拟 CAN DLL | 新增 `PicoATE.CanExampleModule.dll`，实现 `PicoATE_Execute` ABI，解析 raw bytes 和 signal 配置 |
| CAN NativeHost manifest | 新增 `examples/nativehost/can_decode_manifest.json`，DLL 路径通过 `${PICOATE_CAN_DLL}` 注入 |
| CAN sequence 示例 | 新增 `examples/can_dll_sequence.json`，通过 `moduleBindings` 调 NativeHost，不改调度层 |
| 端到端测试 | 新增 transport pass/fail 测试、sequence 执行测试、CLI ctest |
| Runtime/Loop 变量替换 | 新增 `RuntimeVariableResolver`，支持 `${var.sampleIndex}`、`${loop.index}`、`${uut.id}`、`${attempt.number}` |
| Measurement / Limit DTO | 新增 `MeasurementResult`，`NodeResult`/`ExecutionReport`/CLI 均暴露结构化测量值 |
| Loop Report 增强 | `NodeAttempt`/`AttemptReport` 显式携带 loop iteration，`StepReport` 暴露 loop body 归属，CLI 打印每轮信息 |

今日详细记录见 `docs/work_summary_2026-06-24.md`。

## 2026-06-25 今日进展

今天开始处理真实硬件接入前最关键的生命周期问题：DMM/CAN/电源这类设备连接由谁持有、什么时候 connect/disconnect、跨 step 如何保持 session。

| 事项 | 结果 |
|------|------|
| 硬件生命周期设计 | 新增 `docs/hardware_module_lifecycle_design.md`，明确 Scheduler / ResourceManager / DeviceSessionManager / Adapter / Business Module 边界 |
| DeviceSessionManager 核心 | 新增 `DeviceSessionManager`、`IDeviceSession`、`IDeviceSessionFactory`、`DeviceSessionConfig` |
| 连接复用语义 | 同一个逻辑设备已连接时复用 session，close 后再次 open 复用对象但重新 connect |
| 错误路径 | 缺 driver、connect 失败都有明确 `DeviceSessionError` |
| 测试覆盖 | 新增 fake device session 测试，覆盖连接复用、断开、driver 缺失、connect 失败 |

## 2026-06-26 今日进展

今天完成 Station config JSON 解析，把真实硬件地址和 driver 配置从 sequence JSON 中拆出来。

| 事项 | 结果 |
|------|------|
| StationConfig 解析器 | 新增 `StationConfig.h/.cpp`，支持 station 顶层字段和 `devices[]` |
| Device 字段 | 支持 `deviceId/id`、`deviceType/type`、`driverId/driver`、`address/visaAddress`、`lifetime`、`options`、`enabled` |
| DeviceSessionManager 接入 | 新增 `configureDeviceSessions()`，可将 station config 写入 `DeviceSessionManager` |
| StationRuntime | 新增统一运行上下文雏形，持有 `StationConfig` 和 `DeviceSessionManager` |
| CLI `--station` | 命令行运行 sequence 前可加载 station config，并打印工站和设备摘要 |
| PersistentQProcessTransport | 新增长驻进程 transport，适合 fake/真实仪器 host 跨 step 保持 session |
| Fake Instrument Host | 新增 `PicoATE.FakeInstrumentHost.exe`，验证 `open -> read -> read -> cleanup close` 状态不丢 |
| Module-backed Cleanup | `Cleanup` 节点带 `moduleId` 时会执行模块，支持真实硬件 close 放在 cleanup 中 |
| 业务模块访问设备 session | 新增 `IModuleRuntimeServices`，业务模块可通过 `DMM1/CAN1` 逻辑 ID 访问设备 session/command proxy |
| TransportDeviceSession | 新增 `TransportDeviceSessionFactory`，把 station 中的 `fake.dmm/fake.can` 映射到长驻 `PersistentQProcessTransport` |
| Persistent Host 协议深化 | Fake Instrument Host 补充 `health/reconnect/configureDcv/identity/readFrame/shutdown` |
| DMM/CAN Adapter spike | 新增内置 `example.dmm` / `example.can` 模块和 `examples/dmm_can_adapter_sequence.json`，验证 `ConnectDMM/ConfigureDCV/ReadDMM/Disconnect` 风格是否落地舒服 |
| 示例和文档 | 新增 `examples/stations/basic_station.json` 和 `docs/station_config.md` |
| 测试覆盖 | 覆盖变量替换、disabled device、重复 deviceId、错误 lifetime、缺变量、类型错误、StationRuntime、CLI station 示例、persistent instrument 示例和 DMM/CAN adapter 示例 |

## 已完成能力

| 序号 | 模块 | 完成内容 | 状态 |
|------|------|----------|------|
| 1 | 工程骨架 | 建立 VS2022 + Qt6 + CMake 工程，包含核心库和 QtTest 测试 | Done |
| 2 | Runtime 基础模型 | 实现 `ExecutionPlan`、`ExecNode`、`ExecEdge`、`ExecutionFrame`、`NodeActivation`、`NodeAttempt` | Done |
| 3 | ResourceManager | 实现资源租约、互斥申请、等待队列、snapshot waiters | Done |
| 4 | BarrierController | 实现 Batch/Barrier 到达、释放、失败成员移除、snapshot | Done |
| 5 | ExecutionGraphScheduler | 实现同步调度骨架、retry、cleanup 激活、barrier release、stop cleanup 语义 | Done |
| 6 | ExecutionSession | 支持多 UUT 运行、共享调度组件、`requestStop()`、session snapshot 骨架 | Done |
| 7 | ErrorPolicyEngine | 支持 `onFail/onError/onTimeout` 三路策略，区分 Continue/Stop/Retry/Cleanup/Abort | Done |
| 8 | SequenceDef | 实现编辑期模型：`SequenceDef`、`StepGroupDef`、`StepDef`、资源/重试/超时/错误/Barrier 策略 | Done |
| 9 | PlanBuilder | 实现 `SequenceDef -> ExecutionPlan`，生成串行边、Finally 边、CleanupRegion | Done |
| 10 | SequenceCompiler | 实现 `JSON -> SequenceDef -> ExecutionPlan`，含字段解析和错误收集 | Done |
| 11 | 示例文件 | 新增 `examples/simple_sequence.json` 和 `examples/basic_sequence.json` | Done |
| 12 | 技术债记录 | 记录 result DTO 明细不足、PlanBuildResult 值返回所有权问题 | Done |
| 13 | 调度集成测试 | 通过 example JSON 验证端到端运行，覆盖失败 cleanup、retry attempt、resource waiter 释放 | Done |
| 14 | PicoATE.Cli | 新增命令行 demo，支持读取 JSON、编译、运行多 UUT、打印 node/attempt 明细 | Done |
| 15 | ExecutionReport DTO | 新增只读运行结果模型，CLI 不再直接读取 runtime activation 状态 | Done |
| 16 | Disabled/Custom 编译语义 | disabled step/group 不进入 Plan，custom group 按 body 顺序接入执行链 | Done |
| 17 | SequenceCompiler 类型校验 | 对 string/bool/number/object/array 字段和枚举值提供带路径的 compile error | Done |
| 18 | Checkpoint JSON 字段 | 打通 `checkpointBefore/checkpointAfter`：JSON -> StepDef -> ExecNode，并补示例和测试 | Done |
| 19 | JSON Schema 文档 | 新增 `docs/sequence_json_schema.md`，记录字段、类型、默认值、枚举和 unknown-field 规划 | Done |
| 20 | Unknown Field Warnings | 新增 `CompileWarning`，未知字段产生 warning 不阻断编译，`x-*`/`vendor` 作为扩展口 | Done |
| 21 | Module Runtime 边界 | 新增 `IModule`/`ModuleRegistry`/`ModuleResult`，Action 通过模块执行并映射为 `NodeResult` | Done |
| 22 | Module Transport 边界 | 新增 `IModuleTransport`/`TransportModuleAdapter`，为 DLL/Python/子进程模块隔离预留统一调用口 | Done |
| 23 | Project Vision 文档 | 新增 `docs/project_vision.md`，记录三层解耦、配置化交付和业务模块隔离目标 | Done |
| 24 | ModuleTransportJson | 新增 `ModuleTransportRequest/Response` 的 JSON 序列化边界和单元测试 | Done |
| 25 | QProcessTransport + MockHost | 新增 `QProcessTransport` 与独立 `PicoATE.MockHost`，跑通外部进程模块调用 | Done |
| 26 | Module Contract 文档 | 新增 `docs/module_contract.md` 和 `examples/modules/echo_module.py`，记录外部模块协议 | Done |
| 27 | 外部模块配置接入 | 新增 `moduleBindings`、`ModuleBindingRegistrar` 和 `external_echo_sequence.json`，从 JSON 自动注册外部模块 | Done |
| 28 | Python 外部模块示例 | 新增 `python_echo_sequence.json`，通过 `PYTHON_EXE + echo_module.py` 验证真实脚本模块调用 | Done |
| 29 | DllBridgeInvoker 原型 | 新增进程内 `QLibrary` DLL ABI 调用、测试 DLL、成功/错误/超时测试 | Done |
| 30 | NativeHost executable | 新增 `PicoATE.NativeHost.exe`，通过独立进程加载 DLL 并复用 stdio JSON contract | Done |
| 31 | LoopController For 循环 | 新增调度层 `LoopController`、`ExecNodeKind::Loop`、`LoopRegion`、JSON `loop` step、示例和 CLI/Core 测试 | Done |
| 32 | VariableResolver 中心化 | 新增公共 `VariableResolver`，支持内置/显式/环境变量、递归字符串替换、Map/List 深度替换和路径化错误 | Done |
| 33 | NativeHost manifest | 新增 manifest JSON 解析、`--manifest`/`--var` 命令行、示例 manifest、sequence 接入和端到端测试 | Done |
| 34 | 模拟 CAN DLL 示例 | 新增纯软件 CAN 解码 DLL、manifest、sequence、CLI/Core 测试，验证无硬件项目模块接入路径 | Done |
| 35 | Runtime/Loop 变量替换 | 节点执行前解析 runtime payload/inputs，支持 loop/UUT/attempt/var 变量并保留 whole-field 类型 | Done |
| 36 | Measurement / Limit 模型 | 新增 `MeasurementResult`/`MeasurementStatus`，支持 name/value/unit/raw/limits/status，并接入 NodeResult、ExecutionReport、CLI | Done |
| 37 | Loop Report iteration | `ExecutionReport` 显式暴露 loop body 归属和每次 attempt 的 iteration number/value，避免 UI/报表从 attempt 顺序推断 | Done |
| 38 | DeviceSessionManager 生命周期核心 | 新增逻辑设备配置、session factory、连接复用、closeAll 和错误模型，为 DMM/CAN/电源长连接打基础 | Done |
| 39 | Station config JSON | 新增 `StationConfig` 解析、变量替换、设备字段校验、示例文件，并能初始化 `DeviceSessionManager` | Done |
| 40 | StationRuntime + CLI `--station` | 新增 station 运行上下文，CLI 可在运行 sequence 前加载 station config 并打印设备摘要 | Done |
| 41 | PersistentQProcessTransport + FakeInstrumentHost | 新增长驻进程 transport、假仪器 Host、JSON 示例和 CLI/Core 测试，验证跨 step session 状态保持 | Done |
| 42 | 业务模块设备服务 | `ModuleExecutionContext` 注入 `IModuleRuntimeServices`，业务模块可按逻辑设备 ID 调用 `DeviceSessionManager` | Done |
| 43 | TransportDeviceSession | 设备 session 可通过 `IModuleTransport` 调用长驻 host，支持 open/close/health/command proxy | Done |
| 44 | DMM/CAN Adapter spike | 内置 `example.dmm`/`example.can` 调通 fake DMM/CAN host，JSON 示例和 CLI/Core 测试通过 | Done |
| 45 | Engine/UI 工程拆分 | 保留 `PicoATE.sln`，新增独立 `PicoATE.UI.sln` 和联合 `PicoATE.All.sln`；Core 不依赖 Qt Widgets | Done |
| 46 | UI-1 ViewModel 与线程安全 Stop | 新增原子 `StopToken`、`IExecutionService`、Worker 线程、`ExecutionViewModel` 状态机及 Runner 基础绑定 | Done |
| 47 | UI-2 Runner 数据模型 | 新增 Diagnostic/UUT-Step/Attempt/Measurement 模型、UUT 数量输入和选择联动，跑通 basic/for-loop 示例 | Done |
| 48 | UI-3 实时运行监控 | 新增 RuntimeEvent/IRuntimeEventSink、Scheduler/Session/Device 事件、50ms UI 批处理、增量结果模型和设备状态页 | Done |
| 49 | UI-4 报告与历史 | 新增版本化 ExecutionReport JSON、自动历史存储/索引恢复、HistoryModel、报告加载与 JSON/CSV 导出 | Done |
| 50 | 真实 CAN USB DLL 模板 | 新增标准 PicoATE ABI、厂商 SDK 适配层、持久 NativeHost 会话、open/read/write/close、软件回环和验证 Sequence | Done |

## 当前验证

| 验证项 | 命令 | 结果 |
|--------|------|------|
| 配置工程 | `cmake --preset vs2022-qt6` | Passed |
| 编译工程 | `cmake --build --preset vs2022-qt6-debug` | Passed |
| 单元测试 | `ctest --preset vs2022-qt6-debug` | 12/12 passed |
| 独立 UI 编译 | `cd ui; cmake --build --preset vs2022-qt6-debug` | Passed |
| Engine + UI 联合编译 | `cmake --build --preset vs2022-qt6-all-debug` | Passed |
| UI 测试 | `PicoATEUiTests` | 1/1 passed（15 个测试函数） |
| Engine + UI 联合测试 | `ctest --preset vs2022-qt6-all-debug` | 13/13 passed |

## 当前测试覆盖

| 测试方向 | 覆盖内容 |
|----------|----------|
| ResourceManager | 资源互斥、等待队列序列化 |
| BarrierController | Barrier 成员到达、跨 UUT 释放 |
| PlanCache | `shared_ptr<const ExecutionPlan>` 引用语义 |
| Scheduler | Retry、Cleanup、Barrier、Stop、DropFailed member |
| SequenceDef | Setup/Main/Cleanup 建模、重复 step id 检测、策略转换 |
| PlanBuilder | 编辑模型编译成 Plan、CleanupRegion、Finally 边、可运行 Plan |
| SequenceCompiler | JSON 编译成可执行 Plan、不支持 step kind 的错误收集 |
| CLI | `simple_sequence.json` 和 `basic_sequence.json` 可从命令行编译并运行 |
| ExecutionReport | 结果 DTO 包含 UUT、Step、Attempt 明细，覆盖 retry 和失败 cleanup 的 `wasError` 语义 |
| Disabled/Custom | disabled step/group 跳过编译，custom group 参与主执行链，并通过 CLI 示例验证 |
| Schema Type Checks | 字段类型错误、资源模式、错误策略枚举等会返回明确 JSON path |
| Checkpoint Fields | `checkpointBefore/checkpointAfter` 从 JSON 示例到 PlanBuilder 映射全链路覆盖 |
| Unknown Field Warnings | sequence/group/step/resource/retry/timeout/errorPolicy/barrier 的未知字段会产生 warning |
| Module Runtime | 注册模块执行、缺失模块错误、outputs/measurements 映射、显式 `moduleId` JSON 示例 |
| Module Transport | Transport adapter 成功响应、超时、transport error 到 `ModuleResult` 的映射 |
| QProcessTransport | DTO JSON 序列化、MockHost 正常响应、超时、非零退出、ExecutionSession 端到端跨进程调用 |
| Module Bindings | JSON 顶层 `moduleBindings` 解析、变量替换、CLI 自动注册、外部 echo 示例端到端运行 |
| Python Module Example | CMake 注入 `PYTHON_EXE`，JSON 配置调用 `examples/modules/echo_module.py`，验证 outputs/measurements 回传 |
| DllBridgeInvoker | `PicoATE_Execute` C ABI、`QLibrary` 加载、DLL 返回错误码、进程内 worker 线程超时报告 |
| NativeHost | `QProcessTransport -> PicoATE.NativeHost.exe -> TestDllModule.dll` 成功调用，DLL 卡死由父进程 timeout/kill 隔离 | 
| LoopController | 固定 `for` 循环由调度层控制，Plan 不动态修改，循环体节点每轮重新释放执行 |
| VariableResolver | `ModuleBindingRegistrar` 复用公共变量替换规则，后续 NativeHost manifest/station config 不再重复实现 |
| NativeHost manifest | `nativehost_dll_sequence.json` 通过 `--manifest` 调用 DLL，manifest 复用 `VariableResolver` 并保留旧 `--dll` 兼容 |
| Simulated CAN DLL | `can_dll_sequence.json` 通过 NativeHost manifest 调用纯软件 CAN DLL，覆盖 decoded outputs、measurements 和 limit fail |
| Runtime Variables | `for_loop_sequence.json` 使用 `${var.sampleIndex}`、`${loop.value}`、`${uut.id}`、`${attempt.number}` 驱动每轮输入 |
| Measurement DTO | transport JSON、ModuleResult、NodeResult、AttemptReport、StepReport、CLI 全链路覆盖结构化测量值 |
| Loop Report | loop body step 的 `StepReport::loop` 标识归属，每个 `AttemptReport::loopIteration` 标明第几轮和值 |
| DeviceSessionManager | fake device driver 验证 station 级连接复用、close/reopen、缺 driver 和 connect 失败错误 |
| StationConfig | `examples/stations/basic_station.json` 解析成 `DeviceSessionConfig`，并配置 fake `DeviceSessionManager` |
| StationRuntime / CLI Station | `StationRuntime` 加载 station config；CLI `--station` 示例运行前初始化并打印工站设备摘要 |
| Persistent Process Transport | `PersistentQProcessTransport` 复用同一个子进程，fake instrument host 的 `openCount/readCount` 跨调用保持 |
| Module-backed Cleanup | `persistent_instrument_sequence.json` 在 cleanup step 中调用 `fake.instrument.close`，验证 cleanup 可执行模块化收尾 |
| Module Runtime Device Services | `example.dmm`/`example.can` 通过 `context.runtimeServices` 按 `DMM1/CAN1` 调用设备 session，不直接创建 driver |
| Persistent Host Protocol | Fake Instrument Host 覆盖 `health/reconnect/shutdown/configureDcv/identity/readFrame`，为真实长驻 Host 协议打样 |
| DMM/CAN Adapter Spike | `dmm_can_adapter_sequence.json + basic_station.json` 验证 station 逻辑设备、adapter、persistent host、measurement/report 全链路 |
| Runtime Event | Session/UUT/Node/Attempt/Retry/Loop/Barrier/Stop/Cleanup/Device 事件均为只读值对象并带单调序号 |
| UI 实时桥 | Worker 写线程安全缓冲，主线程 50ms 批量刷新，最终 ExecutionReport 覆盖校准 |
| Report JSON | V1 schema 完整往返 UUT/Step/Attempt/Loop/Measurement，拒绝未知未来版本 |
| Report History | 自动保存、原子索引、损坏索引重建、文本筛选和历史报告重新加载 |
| Report Export | JSON 原格式导出；CSV 使用 UTF-8 BOM 并覆盖中文、Limit、Loop 和错误字段 |

## 下一步计划

| 阶段 | 任务 | 目标 | 优先级 |
|------|------|------|--------|
| Phase 5 | 加强调度集成测试 | 覆盖更多端到端场景，例如失败后 cleanup、retry attempt、多 UUT example JSON | Done |
| Phase 6 | PicoATE.Cli | 读取 JSON 文件，编译、运行、打印 UUT/step/attempt 结果 | Done |
| Phase 7 | Result Snapshot DTO | 为 CLI/报告提供只读运行结果视图，避免直接暴露可变 runtime 状态 | Done |
| Phase 8 | SequenceCompiler 增强 | 支持字段类型校验和更清晰的错误路径；未知字段策略待补 | Partial |
| Phase 9 | PlanBuilder 增强 | 支持 disabled step/group、custom group 更明确的编译策略 | Done |
| Phase 10 | Module Transport 边界 | 先抽象跨进程/跨语言 transport contract，不接真实硬件 | Done |
| Phase 11 | QProcessTransport 原型 | JSON over stdio 调用独立 host executable，验证外部模块隔离链路 | Done |
| Phase 12 | Module Contract 雏形 | 固化外部模块请求/响应 JSON DTO，方便 CAN/仪器/产品逻辑模块接入 | Done |
| Phase 13 | 外部模块配置接入 | 通过 JSON `moduleBindings` 自动注册 `QProcessTransport` adapter | Done |
| Phase 14 | Python 示例调用 | 通过 JSON 配置运行独立 Python 模块，验证脚本业务逻辑接入 | Done |
| Phase 15 | DllBridgeInvoker 原型 | 进程内验证 `QLibrary`、DLL C ABI、JSON 请求/响应和超时报告 | Done |
| Phase 16 | NativeHost executable | 独立进程加载 DLL，主进程通过 `QProcessTransport` 调用并隔离崩溃/卡死风险 | Done |
| Phase 17 | LoopController For 循环 | 支持固定 `for` 循环 JSON 配置，循环游标归调度层，业务模块无感 | Done |
| Phase 18 | VariableResolver 中心化 | 抽出公共变量替换服务，支持递归配置替换并服务 moduleBindings/后续 manifest | Done |
| Phase 19 | NativeHost manifest | 用 JSON 配置 DLL 路径、symbol、buffer size、timeout 和 metadata，并支持变量替换 | Done |
| Phase 20 | 模拟 CAN DLL 示例 | 无 CAN 分析仪条件下，用纯软件 DLL 验证项目协议解析/测量/限值接入路径 | Done |
| Phase 21 | Runtime/Loop 变量替换 | 运行期解析 action payload/inputs，loop 每轮能驱动不同模块输入，Plan 保持不可变 | Done |
| Phase 22 | Measurement / Limit 模型 | 将 measurements、limits、判定和报表字段提升为正式 DTO，而不是散落在 outputs 中 | Done |
| Phase 23 | Loop Report 增强 | 显式暴露 loop iteration 元数据，避免 UI 从 repeated attempts 里推断 | Done |
| Phase 24 | 硬件模块生命周期设计与核心模型 | 明确短调用模块、长驻 Host、设备 session、Open/Read/Close 适配边界，并新增 `DeviceSessionManager` | Done |
| Phase 25 | Station config JSON | 描述 DMM1/CAN1/PSU1 的逻辑 ID、driverId、address、lifetime，并初始化 `DeviceSessionManager` | Done |
| Phase 26 | StationRuntime 雏形 | 统一持有 `DeviceSessionManager`，作为 CLI/UI/业务模块共享运行上下文 | Done |
| Phase 27 | Persistent Instrument Host spike | 用 fake host 验证长驻进程、设备 session、health check、shutdown，不先接真实硬件 | Done |
| Phase 28 | 业务模块访问设备 session | 让业务模块按逻辑设备 ID 获取设备代理，而不是直接 new driver | Done |
| Phase 29 | Persistent Host 协议深化 | fake host 补 health/status/reconnect/shutdown/设备命令，明确长驻进程怎么活、怎么死、怎么恢复 | Done |
| Phase 30 | DMM/CAN Adapter spike | 用 fake DMM/CAN 验证 `ConnectDMM/ConfigureDCV/ReadDMM/Disconnect` 和 CAN readFrame 适配方式 | Done |
| Phase 31 | Engine/UI 工程拆分与 Runner 外壳 | 独立 Engine/UI/All 方案，Runner 文件选择与结果容器布局 | Done |
| Phase 32 | ExecutionViewModel / 异步 Runner | 薄 ViewModel、Worker、StopToken、编译/运行/停止和基础报告绑定 | Done |
| Phase 33 | Runner 专用 Models | Diagnostic、UUT/Step、Attempt、Loop 和 Measurement 模型及示例验收 | Done |
| Phase 34 | 实时 Runtime Events | 不可变 Event DTO、Observer、设备状态、UI 限频和最终报告对账 | Done |
| Phase 35 | QProcessTransport/NativeHost/PersistentHost 诊断 | 增强 stderr/stdout 截断、启动失败、协议错误、timeout 的可读诊断 | Medium |

## 最近建议执行顺序

| 顺序 | 任务 | 说明 |
|------|------|------|
| 1 | UI-5 SequenceDocument | 建立可保留扩展字段的 JSON 文档模型和 dirty 状态 |
| 2 | Group/Step 树模型 | 可视化表达 Setup/Main/Custom/Cleanup 和 Loop body |
| 3 | Step 属性编辑器 | 编辑 Resource/Retry/Timeout/Error/Barrier/Loop 等配置 |
| 4 | Undo/Redo 与保存保护 | 增删移动、撤销重做和未保存保护 |
| 5 | Host/Transport 诊断增强 | 独立补 stderr/stdout、timeout 和协议错误诊断，不混入报告格式 |

## 当前结论

PicoATE 已经不只是架构文档，当前具备一个可编译、可测试、可从命令行运行 JSON sequence、能输出只读 ExecutionReport、能通过 `IModule` 执行业务动作，并为外部模块隔离提供 `IModuleTransport` 适配边界的最小调度内核。项目最终目标已记录在 `docs/project_vision.md`：UI、任务调度、业务测试逻辑三层解耦；项目交付时优先通过 JSON/config 配置流程和新增底层业务模块完成，避免修改 UI 和调度框架。当前已经通过 `QProcessTransport`、独立 `PicoATE.MockHost`、`docs/module_contract.md`、JSON `moduleBindings`、真实 Python echo 脚本、进程内 `DllBridgeInvoker`、独立 `PicoATE.NativeHost.exe`、NativeHost manifest、调度层 `LoopController`、中心化 `VariableResolver`、运行期 `RuntimeVariableResolver`、结构化 `MeasurementResult`、Loop Report iteration 元数据、`DeviceSessionManager` 生命周期核心、Station config JSON、`StationRuntime`、CLI `--station`、`PersistentQProcessTransport`、`PicoATE.FakeInstrumentHost.exe`、`IModuleRuntimeServices`、`TransportDeviceSession`、内置 `example.dmm/example.can` 和纯软件 CAN DLL 示例证明业务测试逻辑、外部模块接入、固定循环流程、配置变量替换、运行期 loop 输入驱动、协议解析、限值判定、硬件连接生命周期建模、跨 step session 保持、业务模块按逻辑设备 ID 访问设备和报告消费都可以通过稳定框架能力接入。下一步可以开始做 Qt Runner UI；真实硬件驱动、生产级 Host 诊断和更完整的编辑器仍是后续工作。
