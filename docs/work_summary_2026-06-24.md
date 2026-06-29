# PicoATE Work Summary - 2026-06-24

## 今日目标

今天的目标是回答一个很实际的问题：手上没有 CAN 分析仪时，是否还能继续验证 PicoATE 的项目交付路径。

答案是可以，但验证对象必须明确：

```text
不验证真实 CAN 总线电气层/驱动
验证 JSON 流程配置 -> NativeHost -> DLL -> 协议解析 -> 测量/限值结果
```

所以今天做的是一个纯软件 CAN DLL 示例。它不依赖硬件，不需要 CAN 分析仪，但能证明未来真实项目只需要替换最底层 DLL 或 Python/EXE 模块，UI 和调度框架不需要跟着改。

## 今日完成

| 序号 | 事项 | 结果 |
|------|------|------|
| 1 | 模拟 CAN DLL | 新增 `PicoATE.CanExampleModule.dll`，实现 `PicoATE_Execute(json, out, size)` C ABI |
| 2 | CAN 解码逻辑 | 支持 hex string / byte array 输入，按 `startByte`、`byteLength`、`byteOrder`、`scale`、`offset` 解码 |
| 3 | 限值判定 | 支持 `min/max/unit`，返回 `Passed` 或 `Failed`，并输出 `LimitFail` |
| 4 | NativeHost manifest | 新增 `examples/nativehost/can_decode_manifest.json`，通过 `${PICOATE_CAN_DLL}` 注入 DLL 路径 |
| 5 | Sequence JSON 示例 | 新增 `examples/can_dll_sequence.json`，用 `moduleBindings` 调 NativeHost 加载 CAN DLL |
| 6 | CoreTests 覆盖 | 新增 NativeHost manifest pass/fail 测试和 sequence 端到端测试 |
| 7 | CLI 覆盖 | 新增 `PicoATECliCanDllExample` ctest，CLI 可直接跑 CAN 示例 |
| 8 | Runtime/Loop 变量替换 | 新增 `RuntimeVariableResolver`，loop 每轮可驱动不同 action inputs |
| 9 | Measurement / Limit DTO | 新增 `MeasurementResult`，打通 ModuleResult、NodeResult、ExecutionReport 和 CLI |
| 10 | 文档更新 | 更新 progress plan、project vision、module runtime、NativeHost manifest、README、technical debt |

## 当前验证

验证目录：

```text
D:\Work\ATEdesign\PicoATE_staging
```

验证命令：

```powershell
cmake --build --preset vs2022-qt6-debug
ctest --preset vs2022-qt6-debug
```

验证结果：

```text
9/9 tests passed
```

新增的关键测试：

| 测试 | 验证内容 |
|------|----------|
| `runtimeVariableResolverPreservesTypesAndInterpolatesStrings` | whole-field 变量保留 int/bool 类型，嵌入字符串正常插值 |
| `nodeRunnerResolvesRuntimeVariablesBeforeModuleExecution` | Action 模块收到解析后的 runtime inputs |
| `qProcessTransportCallsSimulatedCanDllManifest` | NativeHost manifest 加载 CAN DLL，解码 `PackVoltage=100.0 V` 并通过限值 |
| `qProcessTransportReportsSimulatedCanLimitFail` | 同一条 CAN 报文在更严格上限下返回 `Failed/LimitFail` |
| `sequenceCompilerRunsSimulatedCanDllExampleFile` | JSON 编译、模块注册、NativeHost、DLL、ExecutionSession、ExecutionReport measurement 全链路运行 |
| `PicoATECliCanDllExample` | CLI 从 JSON 文件直接运行 CAN DLL 示例 |

## 关键结论

### 1. 没有 CAN 分析仪也可以继续测框架

当前阶段最需要验证的是框架边界是否正确：

```text
Sequence JSON
  -> moduleBindings
  -> QProcessTransport
  -> PicoATE.NativeHost.exe
  -> NativeHost manifest
  -> PicoATE.CanExampleModule.dll
  -> ModuleTransportResponse
  -> ExecutionReport / CLI
```

这条链路已经跑通，并且不需要真实硬件。

### 2. 真实硬件验证晚一点接入

等有 CAN 分析仪、真实驱动 DLL、厂商 SDK 或项目报文数据库时，再替换底层模块：

- `PicoATE.CanExampleModule.dll` -> 项目真实 CAN DLL
- 示例 raw bytes -> 实际设备采集到的 CAN frame
- 简化 signal JSON -> 项目 DBC/ARXML/自定义报文解析配置

上层 sequence、NativeHost manifest、调度器和 UI 方向不需要因此改变。

### 3. Measurement / Limit 已有正式 DTO

CAN DLL 返回的测量 JSON 会被框架解析成：

```text
QVector<MeasurementResult>
```

当前覆盖字段：

- measurement name
- value
- unit
- lower/upper limit
- pass/fail result
- raw value
- source step / UUT / attempt / loop iteration

`NodeResult`、`AttemptReport`、`StepReport` 和 CLI 都能直接消费结构化
measurements。`outputs["measurements"]` 暂时保留作为早期兼容出口。

### 4. Loop 变量替换已进入运行期执行边界

`for_loop_sequence.json` 现在可以在 loop body 里使用：

```json
"sampleIndex": "${var.sampleIndex}",
"sampleLabel": "sample-${var.sampleIndex}",
"uutId": "${uut.id}",
"attemptNumber": "${attempt.number}",
"value": "${loop.value}"
```

这些变量在节点执行前由 `RuntimeVariableResolver` 解析。解析发生在
`NodeRunner` 内部的临时 payload 副本上，不修改 `ExecutionPlan`，业务模块
仍然只收到普通输入值。

## 重要文件

| 文件 | 用途 |
|------|------|
| `src/canexamplemodule/CanExampleModule.cpp` | 纯软件 CAN 解码 DLL 示例 |
| `src/core/include/PicoATE/Core/RuntimeVariableResolver.h` | 运行期变量替换接口 |
| `src/core/src/RuntimeVariableResolver.cpp` | 运行期变量替换实现 |
| `src/core/include/PicoATE/Core/MeasurementTypes.h` | 测量/限值 DTO |
| `src/core/src/MeasurementTypes.cpp` | 测量 JSON/map 转换与状态辅助函数 |
| `src/canexamplemodule/CMakeLists.txt` | CAN DLL target |
| `examples/nativehost/can_decode_manifest.json` | NativeHost 加载 CAN DLL 的 manifest |
| `examples/can_dll_sequence.json` | 通过 JSON 配置 CAN 测试项并运行 |
| `tests/CoreTests.cpp` | CAN transport / sequence 端到端测试 |
| `src/cli/CMakeLists.txt` | CLI CAN 示例 ctest |

## 下一步建议

更新：Loop Report iteration 已补入 `NodeAttempt`、`AttemptReport` 和 CLI 输出，下面的计划已调整为后续方向。

| 优先级 | 任务 | 说明 |
|--------|------|------|
| P0 | 当前能力复盘 | 对照 `current_framework_capability_breakdown.md`，确认现有框架能力和真实缺口 |
| P1 | Station config JSON | 硬件生命周期核心模型已开始落地，下一步把逻辑设备 ID、driverId、address、lifetime 配置化 |
| P1 | QProcessTransport 诊断 | 增强 stderr/stdout 截断、启动失败、协议错误、timeout 的可读诊断 |
| P1 | ExecutionReport 增强 | 把 skip reason、cleanup reason、resource history 放到消费者友好的结构里 |
| P2 | 真实 CAN 接入 | 有分析仪或真实项目 DLL 后，再做硬件在环验证 |
| P2 | ViewModel 薄层 | 基于稳定报告 DTO 给未来 Qt UI 准备绑定模型 |

当前方向没有偏移：PicoATE 的目标仍然是 UI、任务调度、业务测试逻辑三层解耦。今天的 CAN 示例恰好证明了这件事：新增一个项目业务 DLL 和 JSON 配置，就能扩展测试能力，而没有改 UI 或调度框架核心语义。
