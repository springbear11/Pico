# DMM 独立插件

本目录遵循 CAN 插件的拆分方式：一个品牌/型号对应一个独立 DLL、Manifest、`driverId`、Station 示例和 Sequence 示例。

| 型号 | DLL | driverId |
|---|---|---|
| Hantek HDM3000 | `PicoATE.DMM.HDM3000.dll` | `plugin.dmm.hdm3000` |
| Keysight 34410A | `PicoATE.DMM.KEYSIGHT34410A.dll` | `plugin.dmm.keysight34410a` |

两种仪器都通过 VISA 使用 SCPI。基础测量功能相近，但不要把它们合并成一个 DLL；型号差异应仅在各自 Vendor 目录中处理。

## 已实现功能

- `open` / `close` / `identity` / `reset` / `clear`
- `configureDcv`、`configureAcv`、`configureDci`、`configureAci`
- `configureResistance2w`、`configureResistance4w`
- `configureFrequency`、`configurePeriod`、`configureDiode`
- `configureContinuity`、`configureCapacitance`
- `read`（`READ?`）
- `query`、`write`（设备调试及厂商扩展）

## 构建

```powershell
Set-Location Pico/templates
cmake --preset vs2022
cmake --build --preset vs2022-debug --target PicoATE_DMM_HDM3000 PicoATE_DMM_KEYSIGHT34410A
```

Station 中的 `pluginPath` 必须指向实际部署的对应 DLL，且 `driverId` 必须匹配同一型号的 `moduleId`。