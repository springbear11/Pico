---
name: can-config
description: 配置、校验和审查 PicoATE 的 CAN 测试脚本（sequence JSON），包括 CAN 插件 open/write/read/close、报文 ID/DLC/载荷、信号解析、Limit 判定和 Station 逻辑设备绑定。Use when writing or reviewing a CAN sequence against the plugin Describe and Station binding.
---

# PicoATE CAN 脚本配置

## 目标

让 CAN 测试脚本与插件能力、Station 逻辑设备绑定、PicoATE 引擎语义对齐：

```text
逻辑设备(CAN1.CH1) → 插件 open/write/read/close → 报文 ID/DLC/载荷 → 信号解析 → Limit 判定
```

## 输入与保护

- 优先收集：目标 sequence `.json`、`StationSystem.json`（设备绑定）、插件 Describe / `PluginRegistry.json`、厂商协议说明。
- 只读分析优先；需要修改脚本时先说明改动点和影响，等待授权。
- 任何可能改变设备状态的实机写入前，明确提示写入地址、值和恢复方式。

## 标准流程

### 1. 逻辑设备与插件绑定

- 脚本优先引用逻辑设备（`CAN1`、`CAN1.CH1`），不写死厂家型号。
- Station 绑定决定实际插件；核对 `deviceId` 与插件 Describe 参数（deviceIndex/channelIndex）是否匹配。

### 2. 插件功能链

- 典型链路：open → write（发送）→ read（接收）→ close。
- 长连接场景把 open/close 放在 Setup/Cleanup，Main 只做收发与解析；确认引擎周期任务与连接生命周期。
- 每次写/读核对：报文 ID、DLC、载荷字节数与预期一致。

### 3. 报文解析

- 从原始帧重建字节，确认 ID/DLC/数据区。
- `decodeRegisters`/`decodeBinary`/文本解析的 `registerOffset`、`offset`、`bitOrder`、`byteOrder`、`scale`、`valueOffset` 与协议定义一致。
- 位号约定（lsb0/msb0）明确后再配置，避免"从左往右数"错误。

### 4. Limit 判定

- 判等、区间、布尔比较与 Modbus skill 相同规范。
- 引用表达式指向当前执行分支的节点输出。
- 监控型重试确认最后一次 attempt 结果。

### 5. 协议复核

- 报文 ID、DLC、信号位定义以厂商协议/插件 Describe 为准；没有定义的位不认定有效。
- 真实硬件与文档不符时记录差异并标注"待实机复核"。

### 6. 多 UUT 共享总线（一拖多）

CAN 是**共享总线**（广播），多个 UUT 同线时不能靠物理连接隔离，必须**地址全局唯一**：

- **源/目标地址唯一**：每个 UUT 的每块控制板地址在总线上唯一（如 UUT1=A=1/B=2，UUT2=A=3/B=4），不能都用默认值。
- **ID 字段语义**：请求帧 `PS=目标地址, SA=本机地址`；响应帧 `SA=来源地址, PS=本机地址`。**响应帧靠 SA 判断是哪个 UUT/枪回的**。
- **手算 CAN ID 后必须回拆验证**：`PS=(ID>>8)&0xFF, SA=ID&0xFF`，确认与目标/来源一致（本次踩坑：B 枪误写 `0x100301C8`，回拆发现 PS=0x01、SA=0xC8 全错）。
- **变量化（不改插件）**：CAN ID 用 perUut 变量，`values` 数组长度 = UUT 数，运行时按当前 UUT 取对应值；脚本不写死 ID。
- **大小端**：确认协议是小端还是大端——ATE-BMS 是**小端**（`decodeBinary` 用 `byteOrder: little`），Modbus TCP 是**大端**，别混。

## 输出格式

- 先给结论（脚本可用 / 需要修改），再列问题清单（绑定、功能链、报文、解析、判定）。
- 每个问题给出：位置（步骤 id）、证据、建议修改。

## 安全边界

- 不修改用户脚本，除非明确要求；先分析后授权。
- 不把发送成功当作接收/解析成功；必须有回读或实测证据。
- 涉及实机写入时，先提示写入地址、值和恢复方式。
