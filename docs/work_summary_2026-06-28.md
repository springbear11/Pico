# PicoATE 工作总结 2026-06-28

## 今日目标

今天从任务引擎阶段正式进入 UI 阶段，优先完成工程边界、异步执行边界和报告数据
模型，不把时间消耗在视觉细节上。

核心原则保持不变：

```text
Qt Widgets
    -> ExecutionViewModel / QAbstractItemModel
        -> IExecutionService / ExecutionWorker
            -> PicoATECore
```

Widget 不直接调用 Scheduler，也不读取 `ExecutionSession::uuts()`、Activation 或
ready queue。

## 今日完成

| 序号 | 工作项 | 完成内容 | 状态 |
|------|--------|----------|------|
| 1 | Engine/UI 工程拆分 | 保留 Engine 方案，新增独立 UI 方案和 Engine+UI 联合方案 | Done |
| 2 | VS2022 默认启动项目 | Engine 默认启动 PicoATECli；UI/All 默认启动 PicoATEUi，避免误启动 ALL_BUILD | Done |
| 3 | UI 规划文档 | 新增 UI 阶段、WBS、工时、线程模型、风险和验收标准 | Done |
| 4 | Runner Web 原型 | 新增可交互工作台原型，验证区域划分和信息密度 | Done |
| 5 | 线程安全 StopToken | 停止请求改为单原子优先级状态，支持 Graceful 升级 Abort | Done |
| 6 | ExecutionSession 停止边界 | Session 只在运行线程消费 StopToken，不再由 UI 线程修改运行状态 | Done |
| 7 | IExecutionService | UI 通过服务接口编译和运行，测试可替换 Fake Service | Done |
| 8 | ExecutionWorker | 工作线程独占 SequenceCompiler、ExecutionSession 和运行生命周期 | Done |
| 9 | ExecutionViewModel | 完成状态机、request ID、Compile/Run/Stop 和命令可用性 | Done |
| 10 | CoreExecutionService | 实现 Sequence/Station 文件加载、编译、模块注册和报告返回 | Done |
| 11 | UI-1 Runner 绑定 | MainWindow 接入 Open、Compile、Run、Stop、Diagnostic 和基础报告 | Done |
| 12 | DiagnosticModel | Error/Warning、路径、消息和建议形成独立表模型 | Done |
| 13 | UutStepModel | 以只读 ExecutionReport 副本构建 UUT/Step 层级模型 | Done |
| 14 | AttemptModel | 显示 Attempt、Outcome、Loop iteration、测量数和错误 | Done |
| 15 | MeasurementModel | 显示名称、值、单位、上下限和判定 | Done |
| 16 | 选择联动 | 选择 Step 刷新 Attempt，选择 Attempt 刷新 Measurement | Done |
| 17 | 多 UUT 输入 | Runner 工具栏增加 1-64 UUT 数量输入，并传入 RunRequest | Done |

## 当前解决方案

| 方案 | 路径 | 默认启动项目 |
|------|------|--------------|
| Engine | `out/build/vs2022-qt6/PicoATE.sln` | PicoATECli |
| UI | `ui/out/build/vs2022-qt6/PicoATE.UI.sln` | PicoATEUi |
| All | `out/build/vs2022-qt6-all/PicoATE.All.sln` | PicoATEUi |

## 今日验证

| 验证项 | 内容 | 结果 |
|--------|------|------|
| Core StopToken | Graceful、Abort 升级、跨线程停止和 Cleanup | Passed |
| ViewModel Fake Service | 状态机、异步线程、失败、过期结果、Stop、销毁 | Passed |
| Model 一致性 | `QAbstractItemModelTester` 检查四个模型 | Passed |
| Simple 示例 | CoreExecutionService 编译并运行 2 UUT | Passed |
| Basic 示例 | 2 UUT Barrier、Measurement 和报告 | Passed |
| For-loop 示例 | 3 次 iteration、Attempt 和 Measurement | Passed |
| 独立 UI 构建 | `cmake --build --preset vs2022-qt6-debug` | Passed |
| 独立 UI 测试 | 1 个测试程序，9 个测试函数 | Passed |
| Engine + UI 联合测试 | 13/13 CTest | Passed |
| UI 启动 | 隐藏窗口启动冒烟检查 | Passed |

## 当前 UI 能力

现在可以：

- 选择 Sequence JSON；
- 选择可选 Station JSON；
- 异步编译并显示 Error/Warning；
- 设置 UUT 数量；
- 异步运行和线程安全停止；
- 显示 UUT 和 Step 层级；
- 显示 Retry Attempt；
- 显示 Loop iteration number/value；
- 显示 Measurement、单位、限值和判定；
- 在 Step、Attempt 和 Measurement 之间联动选择。

当前还不能：

- 在节点执行过程中实时刷新状态；
- 显示 Device/Host 实时健康状态；
- 显示 Resource/Barrier 实时等待状态；
- 保存和查询历史报告；
- 可视化编辑 Sequence 和 Station；
- Pause/Resume/Breakpoint。

## 关键技术结论

### 1. UI 不持有运行中的 Session

`ExecutionWorker` 在工作线程内创建和使用 Session。主线程只保留 ViewModel 状态、
只读报告副本和 StopToken，避免 QObject 线程归属和 Core 可变状态混乱。

### 2. StopToken 不等于强制终止节点

StopToken 在线程间安全传递停止意图，Scheduler 在节点边界消费。若厂商模块或 DLL
调用卡住，仍依赖该模块的 timeout 或 NativeHost 进程隔离；不能从 UI 强杀 C++ 线程。

### 3. Model 不重新解释业务结果

Measurement Pass/Fail、Loop iteration 和 Step outcome 均来自 ExecutionReport。UI
只负责展示，不在界面层重新计算限值或推断调度语义。

### 4. 当前优先数据链，不优先视觉定稿

Web 原型用于确认区域划分。Qt UI 的字体、间距、配色和布局会在数据链稳定后统一
调整，避免视觉返工影响 Runtime Event 和报告模型开发。

## 下一步计划

下一步进入 UI-3：实时运行事件与设备状态。

| 顺序 | 工作项 | 目标 |
|------|--------|------|
| 1 | Runtime Event DTO | 定义 Session/UUT/Node/Device/Host 不可变事件 |
| 2 | Core Observer 边界 | Scheduler/Session 发事件但不依赖 Qt Widgets |
| 3 | Worker Signal Bridge | 通过 queued signal 把事件送回主线程 |
| 4 | 增量模型更新 | 运行中更新 UUT、Step、Attempt 和设备状态 |
| 5 | 合并与限频 | 避免高频事件淹没 UI 事件循环 |
| 6 | 实时集成测试 | 多 UUT、Loop、Stop、Host 错误事件顺序验证 |

UI-3 仍以数据打通为主，不进行大规模视觉重构。
