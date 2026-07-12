# 广成 CAN 硬件闭环验证

## 验证结论

2026-07-03，PicoATE 已通过广成科技真实 USBCAN 分析仪完成第一次硬件闭环。
业务路径不是模拟 DLL：NativeHost 实际加载独立插件工程生成的 CAN 适配 DLL，适配
DLL 再通过 Win32 `LoadLibraryW` 动态加载厂家的 `ECanVci64.dll`，未链接
`ECanVci64.lib`，插件本身也不依赖 Qt。

```text
JSON Sequence
  -> PersistentQProcessTransport
  -> PicoATE.NativeHost.exe
  -> PicoATE.CAN.GCAN.dll
  -> LoadLibraryW(ECanVci64.dll)
  -> CHUSBDLL64.dll / Windows Driver
  -> GCAN USBCAN Device
```

## 实测环境

| 项目 | 实测值 |
|------|--------|
| Windows 设备 | `GCAN USBCAN Device`，驱动状态 OK |
| USB ID | `VID_0C66&PID_000C` |
| 厂家硬件类型 | `USB31` |
| 序列号 | `GC201805427` |
| 自动识别设备类型 | `4`，USBCAN-II |
| 通道数 | `2` |
| 厂家主 DLL | `ECanVci64.dll`，64 位 |
| 厂家依赖 DLL | `CHUSBDLL64.dll` |
| 加载方式 | Win32 `LoadLibraryW` 动态加载，不使用 `.lib` |
| 波特率 | 500 kbit/s |
| CAN 模式 | 厂家 Mode 2，自收发测试 |
| UUT 数量 | 1 |

## 执行流程

使用
`templates/CAN/GCAN/gcan_self_test_sequence.json`：

1. Setup 调用 `open`，动态加载厂家 DLL，依次执行 `OpenDevice/InitCAN/StartCAN`。
2. TestItem 子步骤 TX 发送标准帧 `ID=0x123`、数据 `01 02 03 04 05 06 07 08`。
3. Wait 子步骤等待 50 ms，验证设备句柄能跨 Step 保持。
4. RX 子步骤按 `0x7FF` mask 接收 `ID=0x123`。
5. Limit 子步骤通过 `${step:rx.outputs.dlc}` 读取结果仓库，判断 DLC 必须为 8。
6. Cleanup 调用 `ResetCAN/CloseDevice`，正常释放真实硬件。

## 实测结果

连续执行两次，结果均为：

```text
Open GCAN Device: Passed
CAN_SEND id=0x123 data=01 02 03 04 05 06 07 08
CAN_RECV id=0x123 data=01 02 03 04 05 06 07 08
CAN_RX_DLC = 8 byte Passed
GCAN_ECHO_DLC = 8 byte [8, 8] Passed
Close GCAN Device: Passed
FINAL RESULT: PASSED
```

第二次运行继续成功，说明第一次 Cleanup 已正常关闭设备，不存在句柄占用导致的重开
失败。日志在 DLL 执行过程中实时到达 CLI，不是 Step 完成后一次性回传。

## 已处理问题

第一次运行在加载 `ECanVci64.dll` 阶段报告“找不到指定的模块”。依赖检查确认
主 DLL 还依赖 `CHUSBDLL64.dll` 和 `MSVCR120.dll`。系统已有 64 位 `MSVCR120.dll`，
真正缺失的是依赖 DLL 搜索路径。适配器现已先按绝对路径加载同目录
`CHUSBDLL64.dll`，再加载 `ECanVci64.dll`。

厂家 `BOARD_INFO.can_Num` 在该设备上返回 ASCII 字符 `'2'`，适配层已归一化为数值
通道数 2，避免 UI 显示 50。

迁移到独立插件工程后还发现：厂家 DLL 在 `FreeLibrary` 时会向 stdout 打印裸文本
`deviceind=0,setbaud=3`，污染 NativeHost 的 JSON 协议。GCAN 插件现在在 Cleanup 时只
关闭设备，不在长驻 Host 运行中卸载厂家 DLL；Host 退出时再统一卸载。

## 验证边界

这次使用厂家 Mode 2，证明了真实 USB 设备、Windows 驱动、动态库、CAN 控制器自收发、
跨 Step Session、结果引用、Limit 和 Cleanup 整条链路可用。

它还不能等同于外部物理总线验收。接 DUT 或第二个 CAN 节点后，还应在 Mode 0 下验证：

- CAN_H/CAN_L、共地和 120 欧终端电阻。
- 总线 ACK、真实波形与错误帧。
- DUT request ID/response ID 和协议解析。
- 拔线、Bus-Off、接收超时及恢复策略。
- 多 UUT 共享两个通道时的 Resource 与 Session 所有权。

## 2026-07-12 逻辑设备链真实复测

使用文件：

- `templates/CAN/GCAN/gcan_logical_device_sequence.json`
- `templates/CAN/GCAN/StationSystem.json`
- 逻辑设备：`CAN1`

实际链路：

`Sequence(device/CAN1) -> DeviceSession -> Persistent NativeHost -> PicoATE.CAN.GCAN.dll -> ECANVCI64.dll -> GCAN USB-CAN`

实测结果：

| 阶段 | 结果 |
|---|---|
| Open CAN1 | PASS |
| 发送 ID `0x123`、数据 `01 02 03 04 05 06 07 08` | PASS |
| 等待 50 ms | PASS |
| 接收 ID `0x123`、相同 8 字节数据 | PASS |
| 接收 DLC = 8 byte | PASS |
| 阈值判断 `[8, 8]` | PASS |
| Cleanup Close CAN1 | PASS |
| 最终结果 | PASSED，7/7 节点通过 |

本次复测证明 Sequence 已不需要直接引用 `plugin.can.gcan`。将来更换 ZLG 等型号时，可以保留 `CAN1` 测试流程，只修改 Station 的 `driverId`、`pluginPath` 和连接参数。
