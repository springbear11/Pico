# PicoATE Work Summary 2026-06-26

## 今日目标

把真实硬件地址从 sequence JSON 里拆出去，新增 station config JSON，并继续打通业务模块访问逻辑设备 session 的最小链路：

```text
Sequence JSON
  -> 描述测什么、用哪个逻辑设备 ID

Station config JSON
  -> 描述 DMM1/CAN1/PSU1 的 driverId、address、lifetime、options
```

## 已完成

| 事项 | 结果 |
|------|------|
| StationConfig 解析器 | 新增 `StationConfig.h/.cpp`，支持 station 顶层字段和 `devices[]` |
| Device 字段 | 支持 `deviceId/id`、`deviceType/type`、`driverId/driver`、`address/visaAddress`、`lifetime`、`options`、`enabled` |
| 变量替换 | 复用 `VariableResolver`，支持 `${PROJECT_DIR}`、显式变量和环境变量 |
| DeviceSessionManager 接入 | 新增 `configureDeviceSessions()`，可把 station config 写入 manager |
| StationRuntime | 新增 `StationRuntime`，统一持有 `StationConfig` 和 `DeviceSessionManager` |
| CLI `--station` | CLI 可以在运行 sequence 前加载 station config，并打印 station/device 摘要 |
| PersistentQProcessTransport | 新增长驻进程 transport，同一个子进程可跨多次 module call 保持状态 |
| Fake Instrument Host | 新增 `PicoATE.FakeInstrumentHost.exe`，验证 `open -> read -> read -> cleanup close` |
| Module-backed Cleanup | cleanup step 带 `moduleId` 时会真正执行模块，支持硬件 close/power-off 放在 cleanup |
| 业务模块访问设备 session | 新增 `IModuleRuntimeServices`，`ModuleExecutionContext` 可带运行时服务，业务模块能按 `DMM1/CAN1` 获取设备 session/proxy |
| TransportDeviceSession | 新增 `TransportDeviceSession` / `TransportDeviceSessionFactory`，把 `DeviceSessionManager` 的设备 session 映射到 `IModuleTransport` 长驻 host |
| Persistent Host 协议深化 | Fake Instrument Host 新增 `health/reconnect/configureDcv/identity/readFrame/shutdown` |
| DMM/CAN Adapter spike | 新增内置 `example.dmm` / `example.can`，验证 `ConnectDMM/ConfigureDCV/ReadDMM/Disconnect` 和 CAN `ReadFrame` 风格 |
| 示例 | 新增 `examples/stations/basic_station.json` 和 `examples/persistent_instrument_sequence.json` |
| DMM/CAN 示例 | 新增 `examples/dmm_can_adapter_sequence.json`，配合 `basic_station.json` 可从 CLI 跑 fake DMM/CAN |
| 测试 | 覆盖正常解析、disabled device、变量替换、重复 deviceId、错误 lifetime、缺变量、类型错误、StationRuntime、CLI station、persistent instrument、业务模块设备服务和 DMM/CAN adapter 示例 |
| 文档 | 新增 `docs/station_config.md`，更新 progress plan、hardware lifecycle、vision、technical debt |

## 当前边界

已经完成：

```text
Station config JSON
  -> StationRuntime
  -> StationConfig
  -> DeviceSessionConfig
  -> DeviceSessionManager
  -> CLI station/device 摘要

Persistent instrument sequence
  -> persistent-qprocess module binding
  -> PicoATE.FakeInstrumentHost.exe
  -> open/read/read/status/cleanup close 共用同一个 host 状态

DMM/CAN adapter sequence
  -> example.dmm / example.can
  -> ModuleExecutionContext.runtimeServices
  -> DeviceSessionManager
  -> TransportDeviceSession
  -> PersistentQProcessTransport
  -> PicoATE.FakeInstrumentHost.exe
  -> configure/read/cleanup disconnect
```

还没有完成：

```text
真实 DMM/CAN/PSU driver
生产级 Persistent Instrument Host 诊断和版本/能力协商
真实硬件长时间稳定性验证
```

也就是说，当前已经可以把“这台工站有哪些逻辑设备、真实地址是什么、用哪个 driver”从 sequence 中拆出来，并在 CLI 入口加载。

业务模块现在也已经可以通过运行时服务访问逻辑设备 session/proxy。注意：这次验证仍然用 fake DMM/CAN host，不代表真实 DMM/CAN 硬件已经验证通过。

## 验证

| 验证 | 结果 |
|------|------|
| `cmake --build --preset vs2022-qt6-debug` | Passed |
| `ctest --preset vs2022-qt6-debug` | 12/12 passed |

## 下一步建议

| 优先级 | 任务 | 说明 |
|--------|------|------|
| P0 | Qt Runner UI | 明天先做能打开 JSON/station、编译、运行、显示 UUT/step/attempt/measurement 的最小 UI |
| P0 | ExecutionViewModel 薄层 | UI 只消费 ViewModel / ExecutionReport，不直接读调度内部状态 |
| P1 | QProcess/NativeHost/PersistentHost 诊断 | 接真实 DLL/仪器 Host 前增强错误信息可读性 |
| P1 | 真实项目 adapter spike | 用真实项目风格封装 DMM/CAN/电源 adapter，逐步替换 fake host |
