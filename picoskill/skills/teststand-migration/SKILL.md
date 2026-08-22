---
name: teststand-migration
description: 把 NI TestStand 测试项目迁移为 PicoATE JSON 脚本。负责解读 TestStand Sequence File Documentation（txt/html，GB18030 编码），把 Step 类型、LabVIEW VI、Batch/Lock/Looping/Popup 语义映射为 PicoATE 的 testItem/limit/CAN/periodic/operatorPrompt 等价实现，并处理合并多个 sequence 时的引用重编号与数组类型问题。Use when porting a TestStand .seq project to PicoATE.
---

# PicoATE TestStand 迁移

## 目标

把 NI TestStand 测试项目完整迁移为可编译的 PicoATE JSON 脚本：

```text
TestStand .seq → 导出文档解读 → Step/VI 语义映射 → 生成 PicoATE JSON → 合并/校验 → 实机验证
```

## 输入与保护

- 优先收集：TestStand 项目目录、Sequence File Documentation（txt 或 html，**通常 GB18030 编码**）、原始 `.seq`（二进制）、寄存器映射表、CAN 协议文档（如 `ATE-BMS CAN通信协议V0.2.doc`）、LabVIEW VI 源码。
- **`.seq` 是二进制**（文件头 `TOF1`），不能直接读；可读的是 TestStand 导出的 Documentation（File → Export → Sequence File Documentation）或 XML。
- 只读分析优先；生成脚本后建议先用引擎/UI 编译，再上实机。

## 文件格式识别

| 文件 | 格式 | 读取方式 |
|---|---|---|
| `*.seq` | 二进制（头 `TOF1` = TestStand Sequence File） | 不可直接读，需导出文档 |
| Sequence File Documentation (`*.txt`) | 文本，**GB18030 编码** | PowerShell 用 `[System.Text.Encoding]::GetEncoding("GB18030")` 读 |
| Sequence File Documentation (`*.html`) | HTML，GB18030 | 同上，或解析 HTML |
| 导出 XML (`*.xml`) | UTF-8 XML | 含完整参数/表达式，比 Documentation 信息全 |

常见本地化路径：`..\..\..\..\LV_LIB\...` 是相对 Level 的 LabVIEW 库路径，实际在 `C:\Work\Sinexcel_TS\LV_LIB` 附近。

## Step 类型 → PicoATE 映射

| TestStand StepType | PicoATE 对应 |
|---|---|
| `Numeric Limit Test` | `action`(device/插件) + `decodeRegisters/decodeBinary` + `limit` |
| `Pass/Fail Test` | `action` + `limit`（布尔 `isTrue/isFalse` 或判等） |
| `Action` | `action`（无判定，纯写/初始化） |
| `Wait` | `wait`（`ms`） |
| `Label` | testItem 分组名（作为流程分段标识） |
| `Sequence Call` | PicoATE 不支持 sequenceCall，需**内联展开**或拆成独立可运行脚本 |
| `Statement` | setup 变量赋值 / 引擎变量或 `calculate` |
| `Message Popup` | `operatorPrompt`（`judgment` 模式需操作员确认） |
| `Lock / Unlock` | 单终端串行执行时省略；跨设备互斥用 `resource` |

## LabVIEW VI → 插件映射

| VI | PicoATE |
|---|---|
| `MODBUS_TCP WR_U16/WR_U32.vi` | `writeMultipleRegisters`（`dataFormat: registers`） |
| `MODBUS_TCP RD_U16/RD_U32.vi` | `readHoldingRegisters` + `decodeRegisters` |
| 心跳（`UUT_HeartBeat.vi`）等周期写 | `periodic` 周期任务（intervalMs=5000 写 0x0012） |
| CAN 收发 VI（如 `CCS-ATE_GET_数据量_ChXin.vi`） | CAN 插件 `write`/`read` + `decodeBinary`，29 位扩展帧 |

VI 内部封装的协议细节（CAN ID 计算、参数打包、CRC）不体现在 Documentation 里，需要从协议文档或 VI 源码补。

## CAN 协议要点（ATE-BMS，J1939 派生）

- **29 位扩展帧**，125kbps，小端，`P(3)=0x04 | R(1)=0 | DP(1)=0 | T(1)=0 | PF(8) | PS(8) | SA(8)`。
- `CAN_ID = 0x04<<26 | PF<<16 | PS<<8 | SA`；ATE=100(0x64)，BMS 单播 1~63，广播 64。
- PF：0x01 设置命令 / 0x02 设置响应 / 0x03 获取请求 / 0x04 获取响应。
- 获取请求帧数据 = `[起始索引, 个数]`；响应 = `[起始索引, 个数, 参数数据...]`（小端）。
- CAN 插件过滤器：扩展帧用 `filterMask=0x1FFFFFFF`。

## 一拖多（多 UUT 共享 CAN 总线）配置注意事项

### 区分机制：Modbus 用 Unit ID，CAN 用 PS/SA

| | Modbus TCP | CAN |
|---|---|---|
| 物理 | 点对点 TCP 连接（每 UUT 独立连接） | **共享一条总线**（广播） |
| 区分方式 | Unit ID（`0x0N`，终端号路由） | **PS/SA（29 位帧内的目标/源地址）** |
| 是否天然隔离 | 是（连接隔离） | 否（同线所有板都能收到） |

**关键**：Modbus 用 Unit ID 区分没问题（连接隔离）；CAN 是共享总线，**必须靠地址全局唯一来区分**。

### BMS 地址全局唯一分配（一拖多核心）

同一 CAN 总线上，每个产品的每块 BMS 板地址必须**全局唯一**（协议范围 1~63）。本项目约定：

```text
UUT1: A枪=1, B枪=2
UUT2: A枪=3, B枪=4
```

- 每个 UUT 的 `GunA_BMS_ADDR / GunB_BMS_ADDR` 从工位配置读取（TestStand 的 `My_TestSockets[TS_Index]`），部署时分配不同值。
- **不能**所有 UUT 都用默认 A=1/B=2，否则总线上地址冲突，无法区分响应。

### CAN ID 计算与请求/响应区分（PS/SA 方向相反）

```text
请求帧(ATE→BMS): CAN_ID = 0x04<<26 | PF<<16 | PS(目标BMS)<<8 | SA(ATE=0x64)
响应帧(BMS→ATE): CAN_ID = 0x04<<26 | PF<<16 | PS(ATE=0x64)<<8 | SA(来源BMS)
```

**注意 PS/SA 在请求和响应里是互换的**：
- 请求帧：PS=目标 BMS 地址，SA=100(ATE)
- 响应帧：SA=来源 BMS 地址，PS=100(ATE)——**用响应帧的 SA 判断是哪个 UUT/枪回的**

速查（获取报文 PF=0x03 请求 / 0x04 响应）：

| 枪 | BMS | 请求 ID | 响应过滤 ID |
|---|---|---|---|
| UUT1 A | 1 | `0x10030164` | `0x10046401` |
| UUT1 B | 2 | `0x10030264` | `0x10046402` |
| UUT2 A | 3 | `0x10030364` | `0x10046403` |
| UUT2 B | 4 | `0x10030464` | `0x10046404` |

> 手算 CAN ID 极易出错（本次踩坑：B 枪误写成 `0x100301C8`=PS=0x01,SA=0xC8，完全错误）。**生成后必须回拆验证 PS/SA**：
> `PS=(ID>>8)&0xFF, SA=ID&0xFF`，确认 PS/SA 与目标/来源地址一致。

### 不改插件、用变量满足一拖多

方案：**CAN ID 用 perUut 变量 + 每个 UUT 一组值**，脚本不写死 ID。

- 每把枪需要 2 个变量：请求 ID + 响应过滤 ID（对应 PF 0x03/0x04）。
- 只测"获取"（CAN 侧无设置）时，每枪 2 个变量；若含 CAN 写（PF 0x01/0x02），每枪再加 2 个。
- 本充电测试 BMS 配置走 Modbus，**CAN 只用获取**，所以每枪 2 个变量。

```json
// sequence 顶层 variables（perUut，values 数组长度 = UUT 数）
{ "name": "canReqIdA", "scope": "perUut", "type": "string",
  "values": ["0x10030164", "0x10030364"] },   // UUT1, UUT2
{ "name": "canRcvIdA", "scope": "perUut", "type": "string",
  "values": ["0x10046401", "0x10046403"] },
```

CAN 步骤引用：
```json
{ "function": "write", "inputs": { "data": "04 01", "deviceId": "CAN1.CH1",
  "extended": true, "id": "${var.canReqIdA}", "remote": false } }
{ "function": "read", "inputs": { "deviceId": "CAN1.CH1",
  "filterId": "${var.canRcvIdA}", "filterMask": "0x1FFFFFFF", "timeoutMs": 1500 } }
```

要点：
- `perUut` scope 的 `values` 数组长度 = **UUT 数量**，运行时按当前 UUT 取对应值。
- `filterMask` 固定 `0x1FFFFFFF`（扩展帧全掩码），不用变量。
- `data`（参数起始索引 + 个数）按**参数**区分，不是 CAN ID，不用变量。
- 变量定义丢失风险：合并/重生成脚本时，`variables` 字段可能被覆盖丢失，**生成后要检查顶层 `variables` 是否保留**。

### 大小端：Modbus 大端 vs CAN 小端（别搞混）

- **Modbus TCP 协议固定大端**（高字节在前，插件 `appendU16` 固定大端，无需配置）；设备字序不标准时用 `decodeRegisters.layout` 纠正（normal/swapbytes/reversewords/reverseall）。
- **ATE-BMS CAN 协议明确小端**（多字节低位先发），`decodeBinary` 用 `byteOrder: little`。
- 同一帧 `00 00 11 D0`（大端 U32=0x11D0）与 `11 D0 00 00` 是不同字序，**用真实日志验证设备实际字序**，不要假设 `layout: normal` 一定对。

## 语义等价实现

1. **Looping（"3 次通过/100 次上限"）** → `testItem.retry.maxAttempts=100` + `delayMs`，limit 通过即停（retry 只在失败时继续）。
2. **动态写入值**（"校准电压下发 = BMS 负载电压×10"）→ `values: "[${step:<源>.outputs.value}]"` 运行时变量引用，不写死。
3. **Message Popup 确认** → `operatorPrompt`，注意 `notice`（提示后继续，自动 PASS）vs `judgment`（操作员 PASS/FAIL）区别。
4. **电能差等跨步骤计算** → `calculate`（`operation: subtract`）+ 引用前序 decode 输出 + `limit between`。

## 合并多个 sequence 的坑（重要）

1. **testItem 顶层 id 全局唯一**：跨 group（setup/main/cleanup）也不能重复。
2. **单一元素数组会被 ConvertFrom-Json 解包**：PowerShell `"steps": [ {...} ]` 读进来可能变成对象，序列化后 `steps` 变 `{...}` 而非 `[...]`，编译报 `Expected array, got object`。合并后必须递归把 `steps` 强制包回数组 `@($single)`。
3. **引用重编号必须"单遍"替换**：对 `${step:XXX.` 用正则 `MatchEvaluator` 一次查表替换，**禁止顺序链式 `Replace`**——否则映射目标落在源范围内时（如 `010→019` 而 `019→028`），前一步产物 `019.*` 会被再替换成 `028.*`，产生"引用指向不存在节点 028.09 / 悬空引用"。
4. **引用检查**：编译前验证每个 `${step:XXX.YYY` 的 `XXX` 节点存在、`YYY` 子步骤存在、源在消费前执行（无前向/循环引用）。

## 迁移流程

1. 读 Documentation，列出 Sequence 清单、每步 StepType、VI、limit、时序。
2. 建寄存器速查表（地址/类型/缩放/读写）与 CAN 报文表。
3. 按 sequence 生成 PicoATE JSON（或多个 JSON）。
4. 需要合并则按 MainSequence 顺序内联展开 `Sequence Call`，统一重编号 + 单遍替换引用。
5. 用引擎/UI Compile，修复结构错误（数组、id 唯一、引用）。
6. 实机验证 CAN ID、寄存器地址、动态值是否匹配。

## 安全边界

- 不修改用户 TestStand 源或脚本，除非明确要求。
- 生成脚本后必须编译验证；涉及实机写入前提示地址、值、恢复方式。
- 协议/VI 内部看不全时，明确标注"待实机确认"，不臆测。
