# 创芯 USB-CAN 插件

`PicoATE.CAN.CX.dll` 通过运行时动态加载 `ControlCAN.dll` 接入创芯兼容
USBCAN-I/II 设备。

- 不链接厂家 `.lib`。
- 默认从插件 DLL 同目录加载 64 位 `ControlCAN.dll`。
- 支持通道 0/1、标准帧/扩展帧和常用经典 CAN 波特率。
- 默认设备类型为 `4`（USBCAN-II / CANalyst-II 兼容接口）。
- `gcan_cx_cross_loop_sequence.json` 用于 GCAN 与创芯双通道交叉接线验证。

交叉接线：

- GCAN CAN1 接创芯 CAN2。
- GCAN CAN2 接创芯 CAN1。
- 两侧波特率均为 500 kbit/s，正常模式，关闭自收发。

验证按两个方向串行执行，避免同一个厂家设备被两个独立 Host 同时打开。
