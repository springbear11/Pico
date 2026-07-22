# KORAD USB VISA 程控电源插件

`PicoATE.PSU.KORAD.dll` 使用系统安装的 64 位 VISA Runtime，通过 USB VISA
Resource 与 KORAD 电源通信，不使用 COM 串口接口，也不静态链接 VISA `.lib`。

## 支持指令

- `ISET<X>:<NR2>` / `ISET<X>?`
- `VSET<X>:<NR2>` / `VSET<X>?`
- `IOUT<X>?` / `VOUT<X>?`
- `BEEP<Boolean>` / `OUT<Boolean>`
- `STATUS?` / `*IDN?`
- `RCL<NR1>` / `SAV<NR1>`
- `OCP<Boolean>` / `OVP<Boolean>` / `LOCK<Boolean>`

`STATUS?` 按 8 位状态解析：bit 0 为 CH1 CC/CV，bit 4 为蜂鸣器，bit 5 为 OCP，
bit 6 为输出，bit 7 为 OVP。插件同时接受原始单字节、8 位二进制文本或整数文本返回。

## 构建与配置

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug --target PicoATE_PSU_KORAD
```

构建输出：`templates/bin/Debug/PicoATE.PSU.KORAD.dll`。

先在 VISA 工具中确认设备 Resource，例如：

```text
USB0::0x0000::0x0000::XXXXXXXX::INSTR
```

运行前设置环境变量并加载本目录的 Station 和 Sequence：

```powershell
$env:KORAD_PSU_ADDRESS="USB0::...::INSTR"
```

默认命令不附加换行，适配常见 KORAD USB 协议。如果具体型号要求终止符，可在
`StationSystem.json` 的 `options.writeTermination/readTermination` 中配置，例如 `\n`。

示例会执行 Open、`*IDN?`、配置 OVP/OCP、设置 CH1 电压电流、读取设定值、打开输出、
读取实际电压电流和状态，Cleanup 中始终关闭输出并释放 VISA Session。
