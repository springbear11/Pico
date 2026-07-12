# GCAN 插件

广成科技 `ECanVci64.dll` 的纯 C++ 动态加载实现。

- 不链接 `ECanVci64.lib`。
- 自动从插件 DLL 同目录加载 `CHUSBDLL64.dll` 和 `ECanVci64.dll`。
- 支持 USBCAN-I/II 自动探测、标准波特率、正常/只听/自收发模式。
- 支持标准帧、扩展帧、ID/mask 接收和实时日志。
- Close 时关闭设备但不在长驻 Host 运行中卸载厂家 DLL，避免厂家 DLL 的退出诊断污染
  stdout JSON 管道；Host 退出时再统一卸载。
- 厂商运行库位于 `dependencies/CAN/GCAN/x64`。

真实设备验证见 `gcan_self_test_sequence.json`。

TEST 模式使用同目录 `StationSystem.json`。连接 GCAN 分析仪后，从登录页选择该 Sequence，扫码即可执行
Open -> Transmit -> Wait -> Receive -> DLC Limit -> Close 闭环。
