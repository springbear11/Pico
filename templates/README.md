# PicoATE 独立插件工程

这个目录是独立于任务引擎的插件工作区。它只生成业务插件 DLL，不参与
`PicoATE.sln`、`PicoATE.UI.sln` 或 `PicoATE.All.sln` 的编译。

## 目录结构

```text
templates/
├── PicoATE.Plugins.sln          # VS2022 插件解决方案
├── CMakeLists.txt               # 自动发现插件目录
├── common/                      # PicoATE ABI、JSON、实时日志公共 SDK
├── third_party/                 # 随工程保存的头文件依赖，不依赖系统路径
├── dependencies/                # 厂商运行时 DLL，构建后自动复制
├── CAN/
│   ├── CanAdapter.h             # CAN 统一抽象：open/close/read/write
│   ├── CanPluginBridge.cpp      # CAN JSON -> 抽象接口通用桥
│   ├── CX/                      # 创芯 ControlCAN 动态库实现
│   ├── GCAN/                    # 广成具体实现
│   └── ZLG/                     # 周立功实现预留目录
├── DMM/
│   ├── DmmAdapter.h             # DMM 统一抽象
│   ├── DmmPluginBridge.cpp      # DMM ABI/JSON 通用桥
│   ├── HDM3000/                 # 汉泰 HDM3000 实现
│   └── KEYSIGHT34410A/          # Keysight 34410A 实现
├── PSU/
│   ├── PowerSupplyAdapter.h     # 程控电源统一抽象
│   ├── PowerSupplyPluginBridge.cpp
│   └── KORAD/                   # KORAD VISA 电源实现
├── VISA/
│   ├── VisaAdapter.h            # VISA/DMM 统一抽象
│   └── VisaPluginBridge.cpp     # 通用 ABI 桥，存在厂商实现时才参与编译
└── Modbus/
    ├── ModbusAdapter.h          # Modbus 统一抽象
    ├── ModbusPluginBridge.cpp   # 通用 ABI、参数校验、自描述和实时日志桥
    └── Tcp/
        ├── ModbusTcpAdapter.cpp # Winsock Modbus TCP 实现
        ├── StationSystem.json   # MODBUS1 工站示例
        └── sinexcel_charger_protocol_sequence.json
```

插件工程使用纯 C++20，不依赖 Qt。JSON 使用根目录固定版本的 nlohmann/json，换电脑
后不需要配置 Qt 在 C 盘还是 D 盘；安装 VS2022“使用 C++ 的桌面开发”和 CMake 即可。

## 第一次生成解决方案

双击：

```text
生成解决方案.cmd
```

脚本会在 `.build/vs2022` 保存 CMake 中间项目，并在当前根目录生成
`PicoATE.Plugins.sln`。以后直接打开根目录这个解决方案即可。

也可以使用命令：

```powershell
cmake --preset vs2022
cmake --build --preset vs2022-debug
cmake --build --preset vs2022-release
```

输出目录：

```text
bin/Debug/PicoATE.CAN.GCAN.dll
bin/Debug/PicoATE.CAN.CX.dll
bin/Debug/PicoATE.DMM.HDM3000.dll
bin/Debug/PicoATE.DMM.KEYSIGHT34410A.dll
bin/Debug/PicoATE.PSU.KORAD.dll
bin/Debug/PicoATE.Modbus.Tcp.dll
bin/Release/...
```

插件功能由 DLL 自己的 `PicoATE_Describe` 导出，不再维护旁车
`*.picoate-plugin.json`。把 DLL 和厂家运行库放入应用的 `plugins/` 目录后，在 Admin
界面执行 `Scan Plugins`，统一生成 `plugins/PluginRegistry.json`。详细格式见
`../docs/插件功能清单规范.md`。

## 新增插件

以新增 `CAN/ZLG` 为例：

1. 在 `CAN/ZLG` 新增 `ZlgCanAdapter.h/.cpp`。
2. 实现 `ICanAdapter`。
3. 在 `.cpp` 提供 `std::unique_ptr<ICanAdapter> createCanAdapter()`。
4. 双击一次 `生成解决方案.cmd`，根解决方案会自动出现 `PicoATE.CAN.ZLG` 工程。
5. 编译后自动生成 `PicoATE.CAN.ZLG.dll`。

已有厂商目录内继续增加 `.h/.cpp` 时，CMake `CONFIGURE_DEPENDS` 会在编译时自动发现，
通常不用手改 `CMakeLists.txt`。只有新增一个厂商子目录时，需要重新运行生成脚本刷新
根目录解决方案。

VISA 厂商实现提供 `createVisaAdapter()`，Modbus 厂商实现提供
`createModbusAdapter()`。分类根目录中的通用入口会自动加入每个厂商 DLL，因此厂商
实现不需要重复编写 `PicoATE_Execute`、JSON 分发和实时日志 callback。

当前 `Modbus/Tcp` 是不依赖 Qt 和第三方 Modbus 运行库的 Winsock 实现，支持 FC01、FC02、
FC03、FC04、FC05、FC06、FC0F 和 FC10。`unitId`、寄存器地址和值既可使用十进制，
也可使用 `0x` 十六进制字符串；FC10 还支持寄存器数组及 ASCII/UTF-8 文本打包。FC03/FC04
读回的 `registers` 可交给 Core 内置 `Decode Modbus Registers`，把 Data Type 选择为
ASCII Text 或 UTF-8 Text；不在 TCP 插件中重复实现文本解析。

`IxxxAdapter` 才是 C++ 抽象接口；`XxxPluginBridge.cpp` 不是抽象类，而是把稳定的
PicoATE C ABI/JSON 请求桥接到抽象接口。这里不使用 `VirtualXxxPlugin` 命名，避免
`Virtual` 被误解为模拟设备。没有具体厂商 `.cpp` 的分类不会生成空 DLL。

## 厂商依赖

依赖统一放在：

```text
dependencies/<类别>/<厂商>/x64/
```

构建该厂商插件时，目录中的 DLL 会自动复制到插件输出目录。例如 GCAN 已包含：

```text
dependencies/CAN/GCAN/x64/ECanVci64.dll
dependencies/CAN/GCAN/x64/CHUSBDLL64.dll
dependencies/CAN/GCAN/x64/msvcr120.dll
dependencies/CAN/CX/x64/ControlCAN.dll
```

这里保存的是用户态运行库；真实硬件的 Windows 驱动仍需正常安装。

## 插件和引擎边界

所有插件仍只向 PicoATE 导出稳定 ABI：

```cpp
int PicoATE_Execute(const char* requestJsonUtf8,
                    char* responseJsonUtf8,
                    int responseBufferSize);
```

可选实时日志入口由公共 SDK 自动提供。引擎只加载最终 DLL，不引用本工程里的 C++
接口、厂商头文件或项目文件，因此插件工作区和任务引擎可以各自单独编译。
