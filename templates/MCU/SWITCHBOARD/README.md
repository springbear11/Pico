# ATE MCU 开关板插件

该插件依据 `C:\Work\ATE\ATE MODBUS通讯协议.doc` 实现，插件分类为 `MCU`，驱动 ID 为
`plugin.mcu.switchboard`，生成文件为 `PicoATE.MCU.SWITCHBOARD.dll`。

## 协议

- 物理层：两线 RS485，半双工。
- 串口：19200 baud、8 数据位、无校验、1 停止位。
- 字节序：寄存器高字节先传；CRC16 低字节先传。
- `FC03`：读取 IO 状态。
- `FC10`：写 IO 输出，插件写入后自动用 `FC03` 回读整个寄存器。
- `FC39`：初始化全部 IO，固定数据为 `01 02 03 04`，默认地址为 `0x10`。

`open` 在成功打开串口后会自动发送一次 `FC39` 初始化命令。初始化失败时插件会关闭串口并
返回失败，因此测试脚本不需要再添加独立的初始化步骤。普通 IO 操作使用 Station 中配置的
`Unit ID`（协议默认 `0x00`），初始化地址 `0x10` 由插件内部固定管理。

## IO 映射

| 寄存器 | 开关范围 | 位映射 |
|---|---|---|
| `0x0000` | IO1~IO16 | Bit0=IO1，Bit15=IO16 |
| `0x0001` | IO17~IO32 | Bit0=IO17，Bit15=IO32 |
| `0x0002` | IO33~IO48 | Bit0=IO33，Bit15=IO48 |
| `0x0003` | IO49~IO64 | Bit0=IO49，Bit15=IO64 |
| `0x0004` | IO65~IO80 | Bit0=IO65，Bit15=IO80 |
| `0x0005` | IO81~IO96 | Bit0=IO81，Bit15=IO96 |
| `0x0006` | IO97~IO112 | Bit0=IO97，Bit15=IO112 |

原文第二行写作 `IO(16-32)`，与每寄存器 16 位及后续连续地址不一致；插件按连续范围
IO17~IO32 处理。协议示例说明 IO 顺序为 16→1，因此采用低位对应较小 IO 编号。

2026-08-21 真机验证发现，当前开关板固件会忽略 FC03 请求中的寄存器数量，并从请求
起始地址固定返回到 `0x0008`。例如请求 `0x0000,count=1` 或 `count=7` 均返回 9 个
寄存器。`0x0007~0x0008` 未在协议文档中定义，插件会完整接收最多 9 个寄存器并校验
CRC，但只向上层返回请求范围内、协议已定义的寄存器。

## 构建

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug --target PicoATE_MCU_SWITCHBOARD
cmake --build --preset vs2022-release --target PicoATE_MCU_SWITCHBOARD
```

验证文件：

- `StationSystem.json`：默认端口 `COM3`，正常读写地址 `0x00`。
- `switch_board_verify_sequence.json`：连接时自动初始化，默认执行读取；会改变输出的单路写、
  整组写步骤均设置为 `enabled: false`，确认接线和恢复方案后再手动启用。

## 安全说明

初始化只验证设备是否原样回显 `39 01 02 03 04`。协议没有定义初始化后的确切 IO 值，因此
插件不会把回显成功误报为 IO 状态已验证。设置单路 IO 时，插件会先用 FC03 读取所属 Bank，
只修改目标位，再用 FC10 写回并通过 FC03 回读验证，其他 15 路状态保持不变。上机前仍需确认
目标 IO 对应的负载和安全状态。
