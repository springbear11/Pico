# PicoATE 硬件模块生命周期设计

日期：2026-06-25

这份文档回答一个核心问题：

```text
DMM、CAN、电源、夹具这类真实设备，到底由谁持有连接？
什么时候 connect？
什么时候 disconnect？
跨 step 怎么保持 session？
```

## 1. 先说结论

PicoATE 不应该让单个 Step 随便持有真实设备连接，也不应该让 Scheduler 直接持有 VISA handle、CAN handle 或厂家 DLL handle。

推荐边界是：

```text
ExecutionSession / Scheduler
  -> 只管流程、Retry、Cleanup、Barrier、Loop
  -> 不碰真实设备 handle

ResourceManager
  -> 管“谁现在可以用这个设备”
  -> 解决 DMM1/CAN1/PSU1 的互斥和排队

DeviceSessionManager
  -> 管“这个设备是否已经连接”
  -> 持有逻辑设备 ID 到真实 session 的映射
  -> 负责 connect / disconnect / health check / reconnect

Instrument Adapter
  -> 封装 DMM/CAN/PSU 的具体命令
  -> 例如 ConfigureDCV、ReadDMM、CAN_Read、PSU_SetVoltage

Business Test Module
  -> 表达测试意图
  -> 例如 VerifyPackVoltage、MeasureResistance、CheckPowerRail
```

一句话：

```text
Scheduler 管流程，ResourceManager 管占用，DeviceSessionManager 管连接，Adapter 管驱动命令，业务模块管测试意图。
```

## 2. 连接和占用不是一回事

这点非常重要。

以 DMM 为例：

```text
软件启动或工站初始化:
  Connect DMM1

测试运行中:
  Step A 申请 DMM1 使用权
  ConfigureDCV
  Read
  释放 DMM1 使用权

另一个 UUT:
  申请 DMM1 使用权
  ConfigureResistance
  Read
  释放 DMM1 使用权

软件退出或工站关闭:
  Disconnect DMM1
```

这里：

```text
DeviceSessionManager 管 DMM1 是否已连接。
ResourceManager 管当前谁能用 DMM1。
```

DMM 可以一直连着，但同一时间只能被一个 UUT 独占使用。

## 3. 三种设备生命周期

### 3.1 Step 级生命周期

每个 Step 自己打开、使用、关闭。

适合：

- 纯算法模块
- 协议解析
- 一次性外部 EXE
- 很便宜、无状态的动作

不适合：

- 真实 DMM
- CAN 分析仪
- 电源
- 夹具控制器

原因：

```text
频繁 open/close 慢，而且跨 step 无法保持状态。
```

### 3.2 Run 级生命周期

一次测试运行开始时连接，运行结束或 cleanup 时断开。

适合：

- 某个 sequence 独占一组设备
- 设备只服务当前这一轮运行
- 不希望设备连接长期挂着

例子：

```text
Run start:
  Connect DMM1 / PSU1

Steps:
  多次 Configure / Read / Write

Run cleanup:
  PSU output off
  Disconnect DMM1 / PSU1
```

### 3.3 Station 级生命周期

工站启动时连接，软件退出时断开。

适合真实 ATE 里大多数固定仪器：

- DMM
- 电源
- 电子负载
- 扫码枪
- 夹具控制器
- CAN/LIN 分析仪

好处：

- 连接只建一次
- 可以做 health check
- 可以在 UI 上显示设备状态
- 测试步骤只申请使用权，不重复连接

## 4. 不推荐的 JSON 风格

不建议把 Sequence 写成低级驱动脚本：

```json
[
  { "function": "ConnectDMM" },
  { "function": "ConfigureDCV" },
  { "function": "ReadDMM" },
  { "function": "DisconnectDMM" }
]
```

这样会导致：

- 流程非常碎
- 测试意图不清晰
- UI 很难做得好用
- JSON 变成低级脚本语言
- 最后可能比 TestStand 还难维护

## 5. 推荐的 JSON 风格

Sequence 应该表达测试意图：

```json
{
  "id": "verify-12v-input",
  "kind": "action",
  "moduleId": "project.power",
  "function": "verifyVoltage",
  "resources": [
    { "resourceId": "DMM1", "mode": "exclusive" }
  ],
  "inputs": {
    "dmm": "DMM1",
    "range": 100,
    "lowerLimit": 11.5,
    "upperLimit": 12.5
  }
}
```

业务模块内部做：

```text
从 DeviceSessionManager 获取 DMM1 session
ConfigureDCV
ReadDMM
判断限值
返回 MeasurementResult
```

这样 UI 和调度器看到的是：

```text
Verify 12V Input
```

不是一堆底层驱动函数。

## 6. 工站配置应该放真实地址

Sequence 里建议只写逻辑设备 ID：

```json
{
  "dmm": "DMM1"
}
```

真实 VISA 地址放 station config：

```json
{
  "devices": [
    {
      "deviceId": "DMM1",
      "deviceType": "DMM",
      "driverId": "keysight.34465a.visa",
      "address": "USB0::0x0957::0x0607::MY59001234::INSTR",
      "lifetime": "Station"
    }
  ]
}
```

好处：

- 换工站不改测试流程
- 换仪器不改测试流程
- UI 可以单独做工站配置页面
- ResourceManager 可以继续用逻辑 ID

## 7. 当前已经落地的核心模型

当前代码已经新增：

```text
DeviceSessionManager
IDeviceSession
IDeviceSessionFactory
DeviceSessionConfig
DeviceSessionLifetime
DeviceConnectionState
StationConfig
StationRuntime
PersistentQProcessTransport
PicoATE.FakeInstrumentHost.exe
IModuleRuntimeServices
IDeviceCommandSession
TransportDeviceSession
TransportDeviceSessionFactory
ExampleDmmAdapterModule
ExampleCanAdapterModule
```

位置：

```text
src/core/include/PicoATE/Core/DeviceSessionManager.h
src/core/src/DeviceSessionManager.cpp
src/core/include/PicoATE/Core/StationConfig.h
src/core/src/StationConfig.cpp
src/core/include/PicoATE/Core/StationRuntime.h
src/core/src/StationRuntime.cpp
src/core/include/PicoATE/Core/PersistentQProcessTransport.h
src/core/src/PersistentQProcessTransport.cpp
src/core/include/PicoATE/Core/DeviceTransportSession.h
src/core/src/DeviceTransportSession.cpp
src/core/include/PicoATE/Core/InstrumentAdapterModules.h
src/core/src/InstrumentAdapterModules.cpp
src/fakeinstrumenthost/Main.cpp
```

现在支持：

- 注册设备 session factory
- 配置逻辑设备
- 从 station config JSON 解析逻辑设备
- station config 变量替换
- disabled device 跳过
- 重复 deviceId 检查
- `StationRuntime` 持有 station config 和 `DeviceSessionManager`
- CLI `--station` 加载 station config 并打印设备摘要
- `PersistentQProcessTransport` 复用同一个子进程，适合长驻仪器 Host
- `PicoATE.FakeInstrumentHost.exe` 验证 `open -> read -> read -> cleanup close` 跨 step 状态保持
- Fake Instrument Host 支持 `health/reconnect/configureDcv/identity/readFrame/shutdown`
- Cleanup step 带 `moduleId` 时会真正执行模块，可用于 close/power-off/fixture release
- `ModuleExecutionContext` 注入 `IModuleRuntimeServices`
- 业务模块可通过 `context.runtimeServices->openDeviceSession()` / `invokeDevice()` 按逻辑设备 ID 使用设备
- `TransportDeviceSession` 把 `DeviceSessionManager` 的 session 映射到长驻 host 命令调用
- 内置 `example.dmm` / `example.can` adapter spike，验证 `ConnectDMM/ConfigureDCV/ReadDMM/Disconnect` 和 CAN `ReadFrame`
- `examples/dmm_can_adapter_sequence.json` 通过 station config 跑通 fake DMM/CAN 全链路
- 根据 deviceId 打开 session
- 已连接 session 复用
- closeSession
- closeAll
- 缺 driver 时返回明确错误
- connect 失败时返回明确错误
- 设备状态查询

当前测试覆盖：

- 同一个 `DMM1` 第一次 open 会 create/connect
- 第二次 open 会复用已连接 session
- close 后再次 open 不重新 create，只重新 connect
- driver 未注册会报 `DeviceDriverNotRegistered`
- connect 失败会报 `DeviceConnectFailed`

## 8. 当前还没有做的事情

这次还没有做：

- 真实 VISA DMM driver
- 真实 CAN driver
- 真实 PSU driver
- 生产级 Persistent Instrument Host 诊断、版本协商、能力查询
- UI 设备状态页面
- ResourceManager 和 DeviceSessionManager 的自动联动
- 真实硬件长时间稳定性验证

也就是说：

```text
现在完成的是“连接生命周期核心模型”、“CLI 加载 station config 的入口”、“长驻 Host 的 fake 验证路径”和“业务模块按逻辑设备 ID 访问设备 session/proxy”。
还没有完成“真实硬件长驻 Host 诊断/稳定性”和“真实 driver”。
```

## 9. Persistent Instrument Host 的位置

未来真实硬件建议这样走：

```text
PicoATE 主进程
  -> StationRuntime
  -> DeviceSessionManager
  -> Persistent Instrument Host
  -> DMM/CAN/PSU Adapter
  -> VISA / 厂家 DLL / SCPI
```

Persistent Host 是长驻进程。

它负责：

- 连接设备
- 持有 handle
- 接收 PicoATE 请求
- 执行具体仪器动作
- 返回结果
- cleanup/shutdown 时关闭设备

这和当前 `QProcessTransport` 的区别：

| 方式 | 特点 |
|------|------|
| 当前 QProcessTransport | 每次调用启动短进程，简单安全，但不适合跨 step 持有 handle。 |
| Persistent Instrument Host | 长驻进程，适合真实硬件，但需要 session 协议、状态管理和诊断。 |

## 10. DMM Adapter 应该怎么放

你设想的接口类似：

```cpp
class IDmm {
public:
    virtual void connect(const std::string& visaAddress) = 0;
    virtual void disconnect() = 0;
    virtual std::string identity() = 0;
    virtual void reset() = 0;
    virtual void clearStatus() = 0;
    virtual void configureDCV(double range, double nplc) = 0;
    virtual double read() = 0;
    virtual std::string query(const std::string& scpi) = 0;
};
```

这个方向是对的。

但它应该属于：

```text
Instrument Adapter 层
```

不应该属于：

```text
Scheduler 核心接口
```

Scheduler 不应该知道 `ConfigureDCV`。

业务模块可以知道。

## 11. 最近完成和推荐下一步

刚完成：

| 顺序 | 任务 | 说明 |
|------|------|------|
| 1 | 业务模块访问设备 session | 业务模块已经能按逻辑 ID 获取设备代理，而不是自己 new driver |
| 2 | Persistent Instrument Host 协议深化 | Fake host 已补 `health/reconnect/shutdown` 和 DMM/CAN 最小命令 |
| 3 | DMM/CAN adapter spike | 已用 fake DMM/CAN 验证 `ConnectDMM/ConfigureDCV/ReadDMM/Disconnect` 和 `ReadFrame` |

下一步建议：

| 顺序 | 任务 | 说明 |
|------|------|------|
| 1 | UI Runner / 设备状态页面 | 在 UI 中显示 station、device、session、health 和运行结果 |
| 2 | ResourceManager 和 DeviceSessionManager 联动 | 资源占用和设备连接状态要能共同诊断 |
| 3 | Host/Transport 诊断增强 | 接真实 DLL/仪器 Host 前，先把 timeout、stderr、协议错误做得更好排查 |
| 4 | 真实项目 adapter spike | 用真实项目风格替换 fake DMM/CAN，验证适配层接口是否仍然舒服 |

## 12. 当前判断

这个方向没有偏。

但要守住边界：

```text
不要把 JSON 写成底层驱动脚本。
不要让 Scheduler 知道具体仪器命令。
不要把真实设备 handle 放在短生命周期 Step 里。
```

只要这三个原则守住，后面 DMM/CAN/电源都可以逐步接进来。
