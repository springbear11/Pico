# PicoATE Station Config JSON

日期：2026-06-26

Station config 用来描述“这个工站有哪些设备，以及这些设备真实地址是什么”。

它不描述测试流程。

测试流程仍然放在 sequence JSON。

## 1. 为什么需要 Station Config

Sequence JSON 应该写逻辑设备 ID：

```json
{
  "dmm": "DMM1"
}
```

不要把真实 VISA 地址直接写进测试流程：

```json
{
  "visaAddress": "USB0::0x0957::0x0607::MY59001234::INSTR"
}
```

真实地址属于工站，不属于测试流程。

这样换工站、换仪器、换地址时，不需要改 sequence。

## 2. 文件结构

示例见：

```text
examples/stations/basic_station.json
```

基本格式：

```json
{
  "stationId": "bench-01",
  "name": "PicoATE Bench 01",
  "metadata": {
    "site": "lab"
  },
  "devices": [
    {
      "deviceId": "DMM1",
      "deviceType": "DMM",
      "driverId": "fake.dmm",
      "address": "${DMM1_ADDRESS}",
      "lifetime": "Station",
      "options": {
        "defaultFunction": "DCV",
        "nplc": 10
      }
    }
  ]
}
```

## 3. 顶层字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `stationId` / `id` | string | 否 | 工站 ID。 |
| `name` | string | 否 | 工站显示名。 |
| `metadata` | object | 否 | 工站元数据。 |
| `devices` | array | 是 | 设备列表。 |

## 4. Device 字段

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `deviceId` / `id` | string | 是 | 逻辑设备 ID，例如 `DMM1`、`CAN1`、`PSU1`。 |
| `deviceType` / `type` | string | 否 | 设备类型，例如 `DMM`、`CAN`、`PSU`。 |
| `driverId` / `driver` | string | 是 | 设备 session factory ID，例如 `keysight.34465a.visa`。 |
| `address` / `visaAddress` | string | 否 | 真实地址，例如 VISA 地址、IP、串口名、DLL 配置路径。 |
| `lifetime` | string | 否 | `Step`、`Run`、`Station`，默认 `Station`。 |
| `options` | object | 否 | driver 专属配置。 |
| `enabled` | bool | 否 | 默认 `true`；`false` 时忽略该设备。 |

## 5. Lifetime

| 值 | 意思 | 适合 |
|----|------|------|
| `Step` | 每次 step 自己打开/关闭 | 纯算法、一次性脚本、便宜无状态动作。 |
| `Run` | 一次运行期间保持连接 | 当前 sequence 独占的设备。 |
| `Station` | 工站级连接，长期保持 | DMM、CAN、电源、夹具、扫码枪等固定设备。 |

大小写、下划线、短横线、空格会被宽松处理。

例如：

```text
station
Station
station_level
```

后续如果需要 `StationLevel` 这种别名，再扩展枚举解析。

当前实现支持的标准值是：

```text
step
run
station
```

## 6. 变量替换

Station config 复用 `VariableResolver`。

支持：

```text
${PROJECT_DIR}
${SEQUENCE_DIR}
显式 variables
环境变量
```

示例：

```json
{
  "address": "${DMM1_ADDRESS}"
}
```

测试里通过：

```cpp
VariableResolverOptions options;
options.variables.insert("DMM1_ADDRESS", "USB0::...");
```

传入解析器。

## 7. 当前代码入口

核心 API：

```cpp
StationConfigResult loadStationConfigFile(
    const QString& filePath,
    VariableResolverOptions resolverOptions = {});

StationConfigResult parseStationConfigJson(
    const QJsonObject& object,
    const VariableResolverOptions& resolverOptions = {});

QVector<StationConfigDiagnostic> configureDeviceSessions(
    const StationConfig& config,
    DeviceSessionManager& manager);
```

运行期统一入口：

```cpp
class StationRuntime {
public:
    StationRuntimeResult loadStationConfigFile(
        const QString& filePath,
        VariableResolverOptions resolverOptions = {});

    StationRuntimeResult applyStationConfig(const StationConfig& config);

    bool hasStationConfig() const;
    const StationConfig& stationConfig() const;

    DeviceSessionManager& devices();
    const DeviceSessionManager& devices() const;
};
```

位置：

```text
src/core/include/PicoATE/Core/StationConfig.h
src/core/src/StationConfig.cpp
src/core/include/PicoATE/Core/StationRuntime.h
src/core/src/StationRuntime.cpp
```

## 8. 当前已经完成

已经完成：

- station config JSON 解析
- `devices[]` 类型校验
- `deviceId/id`、`deviceType/type`、`driverId/driver`、`address/visaAddress` 别名
- `enabled: false` 跳过设备
- `lifetime` 解析
- 重复 `deviceId` 报错
- 变量替换
- `options` 保留结构化配置
- `configureDeviceSessions()` 将 station config 写入 `DeviceSessionManager`
- `StationRuntime` 统一持有 station config 和 `DeviceSessionManager`
- CLI 支持 `--station`
- CLI 会为 `fake.dmm` / `fake.can` 注册 fake instrument device session factory
- 业务模块可通过 `ModuleExecutionContext.runtimeServices` 使用 station 中的逻辑设备
- `examples/dmm_can_adapter_sequence.json` 已验证 `DMM1/CAN1` 从 station 到 adapter 再到 fake host 的链路
- 示例文件
- 单元测试
- CLI 示例测试

## 9. 当前还没完成

还没完成：

- `ExecutionSession` 自动接收 station config
- 真实 DMM/CAN/PSU driver
- 生产级 Persistent Instrument Host 诊断、版本协商、能力查询
- UI 工站配置页面

也就是说：

```text
Station config 现在能解析、能校验、能配置 DeviceSessionManager，
也可以通过 CLI --station 在运行 sequence 前加载并打印设备摘要。

业务模块现在已经可以通过 runtime services 使用逻辑设备。

但它还没有验证真实硬件，也还没有生产级设备状态/诊断 UI。
```

## 10. 推荐下一步

下一步建议：

| 顺序 | 任务 | 说明 |
|------|------|------|
| 1 | UI 工站配置页面 | 后续在 Qt UI 里显示 station、device 状态和诊断。 |
| 2 | Host/Transport 诊断增强 | 接真实硬件前补清楚 timeout、stderr、协议错误。 |
| 3 | 真实 DMM/CAN/PSU driver | 用项目 adapter 替换 fake host。 |
| 4 | ExecutionSession / StationRuntime 合并入口 | 后续 UI 可以只持有一个统一运行上下文。 |

## 11. CLI 用法

命令行可以在运行 sequence 前加载 station config：

```powershell
$env:DMM1_ADDRESS="USB0::0x0957::0x0607::MY59001234::INSTR"
.\out\build\vs2022-qt6\src\cli\Debug\PicoATE.Cli.exe run examples\simple_sequence.json --station examples\stations\basic_station.json
.\out\build\vs2022-qt6\src\cli\Debug\PicoATE.Cli.exe run examples\dmm_can_adapter_sequence.json --station examples\stations\basic_station.json
```

当前输出会包含：

```text
Station: PicoATE Bench 01 [bench-01]
Devices: 2 configured
  - DMM1 [DMM] fake.dmm lifetime=Station address=USB0::...
  - CAN1 [CAN] fake.can lifetime=Run address=...
```
