# SCOPE 独立插件（示波器）

本目录遵循 CAN/DMM 插件的拆分方式：一个品牌/型号对应一个独立 DLL、`driverId`、Station 示例和 Sequence 示例。

| 品牌 | DLL | driverId |
|---|---|---|
| Rigol（DS1000Z 系列及同 SCPI 语法的示波器） | `PicoATE.SCOPE.RIGOL.dll` | `plugin.scope.rigol` |

示波器通过 VISA 使用 SCPI。抽象接口 `ScopeAdapter.h` 与具体型号无关，型号差异只在各自 Vendor 目录中处理（Rigol 全系示波器 SCPI 指令基本统一，`RIGOL` 目录即其实现）。

## 抽象接口（ScopeAdapter.h）

`IScopeAdapter` 提供以下业务能力（全部方法返回 `Result`，模式参数用 `int` 常量，子类实现时做范围校验）：

- **生命周期**：`connect` / `disconnect` / `isConnected`
- **IEEE488.2**：`identity` / `reset` / `clear` / `lockFrontPanel`
- **运行控制**：`run` / `stop` / `single` / `forceTrigger`
- **通道配置**：`enableChannel` / `setChannelCoupling` / `setProbe` / `setVoltageDiv` / `setChannelOffset` / `setInvert` / `setBandwidthLimit`
- **时基**：`setTimebaseScale`
- **触发**：`setTriggerMode` / `setTriggerSweep` / `setTriggerCoupling` / `setEdgeTriggerSource` / `setEdgeTriggerLevel` / `setEdgeTriggerSlope`
- **测量**：`measure`（`:MEASure:ITEM?`）
- **自动设置**：`autoScale`
- **自定义指令**：`query` / `write`

## 已实现功能（RIGOL 插件）

`open` / `close` / `identity` / `reset` / `clear` / `lockFrontPanel` / `run` / `stop` / `single` / `forceTrigger` / `enableChannel` / `setChannelCoupling` / `setProbe` / `setVoltageDiv` / `setChannelOffset` / `setInvert` / `setBandwidthLimit` / `setTimebaseScale` / `setTriggerMode` / `setTriggerSweep` / `setTriggerCoupling` / `setEdgeTriggerSource` / `setEdgeTriggerLevel` / `setEdgeTriggerSlope` / `measure` / `autoScale` / `query` / `write`

## 构建

```powershell
Set-Location Pico/templates
cmake --preset vs2022
cmake --build --preset vs2022-debug --target PicoATE_SCOPE_RIGOL
```

> **功能详解见 [`FUNCTIONS_GUIDE.md`](FUNCTIONS_GUIDE.md)**：28 个功能的原理、SCPI 命令、测量影响、使用场景与踩坑记录（面向测试序列编写者）。

## 真机验证（Rigol DS1104Z Plus / USB VISA）

`rigol_scope_verify_sequence.json` + `StationSystem.json` 已在真机跑通（CLI 端到端 16/16 PASS），CH1 接 CAL 标准方波测得：

- `measure VPP` = 3.08 V（CAL 输出 ~3Vpp）
- `measure FREQuency` = 999.9999 Hz（CAL 输出 1kHz）

### 已知注意事项

1. **复位/自动设置后需等待再查询**：`*RST`、`:AUToscale` 处理期间仪器忙，紧跟的查询会 VISA 超时（插件返回 `VisaReadFailed`）。序列里已在 reset/run 后加了 wait。
2. **超时后残留响应已排空**：读超时后插件会丢弃迟到字节，避免下一次查询读到错位响应。
3. **错误信息固定英文**：不依赖 `viStatusDesc`（中文版 NI-VISA 返回 GBK 文本会使 JSON 序列化崩溃）。

Station 中的 `pluginPath` 必须指向实际部署的对应 DLL，且 `driverId` 必须匹配同一型号的 `moduleId`（`plugin.scope.rigol`）。
