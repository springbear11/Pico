# PicoATE CAN USB 硬件 DLL 模板

这个模板把厂商 CAN SDK 包装成 PicoATE 标准 DLL。PicoATE 和 JSON 协议相关代码已经
写好，真实硬件接入时主要修改 `VendorCanAdapter.cpp`，不需要改 Scheduler 或 UI。

## 1. 当前能力

| function | 作用 | 连接要求 |
|----------|------|----------|
| `open` / `ConnectCAN` | 打开设备和 CAN 通道、设置波特率 | 无 |
| `status` | 查询当前连接状态 | 无 |
| `write` | 发送 CAN/CAN-FD 帧 | 已 open |
| `read` | 按 ID/mask 等待一帧 | 已 open |
| `requestResponse` | 发送一帧并等待响应 | 已 open |
| `close` / `Disconnect` | 停止通道并关闭设备 | 无 |

DLL 只导出一个稳定入口 `PicoATE_Execute`。JSON 中的 `function` 决定调用 open/read/
write/close，厂商 SDK 的多个函数不需要逐个导出给 PicoATE。

## 2. 先跑软件回环

模板默认开启 `PICOATE_CAN_TEMPLATE_SIMULATION=ON`，不需要硬件：

```powershell
cmake -S templates/can_hardware_dll `
      -B templates/can_hardware_dll/out/build `
      -G "Visual Studio 17 2022" -A x64 `
      -DCMAKE_PREFIX_PATH=D:/QT/6.9.1/msvc2022_64
cmake --build templates/can_hardware_dll/out/build --config Debug
```

然后用 UI 打开 `can_hardware_sequence.json`，或运行：

```powershell
out/build/vs2022-qt6-all/src/cli/Debug/PicoATE.Cli.exe `
  templates/can_hardware_dll/can_hardware_sequence.json
```

## 3. 接入真实厂商 SDK

1. 将厂商 `.h` 放进项目目录，或设置 `VENDOR_CAN_INCLUDE_DIR`。
2. 设置厂商 import library：`VENDOR_CAN_LIBRARY=C:/Vendor/lib/vendor_can.lib`。
3. 在 `VendorCanAdapter.cpp` 的五个 TODO 位置映射厂商 API。
4. 关闭模拟模式：`-DPICOATE_CAN_TEMPLATE_SIMULATION=OFF`。
5. 保证模板、厂商 DLL、Qt 和 PicoATE NativeHost 都是 x64。
6. 把厂商运行时 DLL 放到 NativeHost/DLL 同目录或系统 `PATH`。

常见映射：

```text
Vendor_OpenDevice / VCI_OpenDevice       -> VendorCanAdapter::open
Vendor_InitCAN / VCI_InitCAN             -> VendorCanAdapter::open
Vendor_StartCAN / VCI_StartCAN           -> VendorCanAdapter::open
Vendor_Transmit / VCI_Transmit           -> VendorCanAdapter::transmit
Vendor_Receive / VCI_Receive             -> VendorCanAdapter::receive
Vendor_ResetCAN + Vendor_CloseDevice      -> VendorCanAdapter::close
```

厂商返回码必须在适配层转换成可读 `errorMessage`，不要把裸整数错误码直接丢给 UI。

## 4. 为什么必须 persistent-qprocess

CAN 句柄需要跨 Step 保持：

```text
Setup/open -> Main/write/read -> Cleanup/close
```

示例使用 `persistent-qprocess`，同一个 NativeHost 在整次运行中保持存活。NativeHost
内的 DLL也保持加载，因此 `VendorCanAdapter` 中的句柄不会在 Step 之间丢失。不要把
transport 改成普通 `qprocess`，否则每个 Step 都会启动新进程。

## 5. 第一次硬件验证建议

1. 首次只运行 1 个 UUT，避免多个 UUT 同时共享一个物理 CAN 通道。
2. 先只保留 open/status/close，确认驱动、位数、设备索引和通道号。
3. 再增加 write，使用分析仪自带软件观察是否真的发出帧。
4. 最后配置 DUT 的真实 request ID、response ID 和波特率测试 read/requestResponse。
5. CAN 总线需要正确接地和终端电阻；不要仅凭 SDK 返回成功判断物理总线正常。

模板通过全局 mutex 串行调用厂商 SDK，适合单分析仪、单通道的首次验证。量产时若多
UUT 共享分析仪，应把会话提升到 `DeviceSessionManager` 并明确通道、资源和 Cleanup
所有权，而不是让多个 UUT 任意调用同一个 close。

## 6. 我还需要的厂商信息

要把 TODO 直接改成可用实现，需要 CAN 分析仪型号、厂商 SDK 头文件，以及 open/
init/start/transmit/receive/close 的函数声明和返回码说明。
