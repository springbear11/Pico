# PicoATE UI 工程结构

完整的 UI 分阶段计划、WBS、工时、线程模型和验收标准见
[`ui_development_plan.md`](ui_development_plan.md)。

## 目标

任务引擎和 Qt UI 使用独立的构建入口。UI 只依赖引擎公开接口，引擎不引用
UI 类，也不依赖 Qt Widgets。

```text
PicoATEUi
    -> ExecutionViewModel（下一阶段）
        -> PicoATECore 公开 API
```

当前 UI 已通过 `ExecutionViewModel -> ExecutionWorker -> IExecutionService`
接入异步编译、运行和停止。Widget 不直接调用 `ExecutionSession`，运行中的
Session 由工作线程独占，主线程只通过线程安全 `StopToken` 发出停止请求。

## VS2022 解决方案

| 解决方案 | 生成路径 | 包含内容 | 用途 |
|----------|----------|----------|------|
| Engine | `out/build/vs2022-qt6/PicoATE.sln` | Core、CLI、Hosts、DLL 示例、CoreTests | 不启动 UI，独立开发和测试任务引擎 |
| UI | `ui/out/build/vs2022-qt6/PicoATE.UI.sln` | PicoATEUi 及其 PicoATECore 依赖 | 独立开发 Qt Runner |
| All | `out/build/vs2022-qt6-all/PicoATE.All.sln` | 全部引擎目标、测试和 PicoATEUi | 一次编译和调试完整工作区 |

生成方案时，Engine 默认启动项目为 `PicoATECli`，UI 和 All 默认启动项目为
`PicoATEUi`。`ALL_BUILD` 只是 CMake 的批量构建目标，不能作为程序启动。

## 构建命令

只构建引擎：

```powershell
cmake --preset vs2022-qt6
cmake --build --preset vs2022-qt6-debug
ctest --preset vs2022-qt6-debug
```

只构建 UI：

```powershell
cd ui
cmake --preset vs2022-qt6
cmake --build --preset vs2022-qt6-debug
```

一起构建 Engine 和 UI：

```powershell
cmake --preset vs2022-qt6-all
cmake --build --preset vs2022-qt6-all-debug
```

## 依赖规则

1. `PicoATECore` 可以依赖 Qt Core，但不能依赖 Qt Widgets。
2. `PicoATEUi` 可以链接 `PicoATECore`、Qt Core 和 Qt Widgets。
3. Widget 不得读取 Scheduler ready queue、activation 或可变 UUT 状态。
4. UI 状态由 `ExecutionViewModel` 和只读报告 DTO 提供。
5. UI 命令在 ViewModel 边界异步执行，测试任务不能阻塞 Qt 事件循环。
6. CLI 继续作为同一引擎的独立消费者，不依赖任何 UI 代码。

## 当前 Runner 外壳

首版 `PicoATEUi` 已提供：

- Sequence JSON 文件选择；
- Station JSON 文件选择；
- 固定的 UUT/Step 结果树列；
- Diagnostics 和 Measurements 页签；
- Compile、Run、Stop 命令；
- 编译诊断和最终 UUT/Step 报告的基础绑定；
- `ExecutionViewModel` 状态机和异步 Worker。
- Diagnostic、UUT/Step、Attempt、Loop 和 Measurement 正式模型；
- 可配置的多 UUT 运行数量；
- Step -> Attempt -> Measurement 选择联动。

实时进度、设备状态和 Host 状态属于 UI-3。
