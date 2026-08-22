---
name: modbus-config
description: 根据用户给出的寄存器地址、长度、读写内容和判定条件，对照寄存器映射表生成 PicoATE Modbus TCP 测试脚本（sequence JSON），格式遵循现有脚本规范（10_设置/03_查询/解析/Limit Check），并负责十六进制换算、U16/U32 组合、位偏移换算和回读验证。Use when creating or reviewing a Modbus sequence against the register map.
---

# PicoATE Modbus 脚本配置

## 目标

让用户用一句话描述需求（地址、长度、写什么、查什么、判什么），代理直接查寄存器表并生成符合现有脚本风格、可直接编译运行的 Modbus sequence JSON：

```text
用户需求 → 寄存器表查证 → 数值/位换算 → 10_设置 / 03_查询 / 解析 / Limit 节点 → 可运行脚本
```

本技能默认针对 Modbus TCP；若设备是 Modbus RTU，帧诊断改为校验从站地址、功能码、字节数和 CRC，**不使用 MBAP/Transaction ID 规则**。RTU 固件可能存在稳定的非标准读尾行为：不能机械要求返回寄存器数等于请求 `count`，应以完整有效帧和设备实测行为为准，并由适配层裁剪为业务请求的范围。

## 输入约定（用户通常这样说）

- 地址：如 `0xA007`、`0xA411`（十六进制优先；十进制需换算并注明）。
- 长度：寄存器数量（`count`）；U16=1、U32=2、U64=4、`char[N]` 按 2 字节一寄存器。
- 写什么：`values`（寄存器数组）或文本（`asciiText/utf8Text` + `registerCount`）。
- 查什么：读取后要解析的字段名/位定义。
- 判什么：期望值/上下限/布尔（可选；没有判定需求也要提示是否加 Limit）。

## 标准流程

### 1. 查寄存器表

用 `shared/寄存器映射表_协议合并版.md` 核对：

- 地址归属区段与有效 Unit ID（公共 `0x00`、M2 `0x0F`、CCU `0x01~0x0E`、MCU `0x00`）。
- 数据类型、寄存器数、R/W 属性。
- 缩放说明（如 `0.1V/bit`、`偏移量-50`）与位定义。
- 表中没有的地址/位：明确告知"协议表未定义，需实机确认"，不臆测。

### 2. 数值与寄存器换算

- **物理值 → 寄存器**：`寄存器值 = (目标值 - valueOffset) / scale`，再按数据宽度拆分。
- U16 占 1 个寄存器；U32 占 2 个寄存器，**高字在前**（如 10000 = `0x2710` → `[0x0000,0x2710]`）。
- 位字段：协议位号通常从 1 开始，`decodeBinary` 用 **0-based 偏移**，需换算（协议 Byte/位 → 0-based offset）。
- 十六进制写 `0x` 前缀，十进制直接写数字；`values` 用 JSON 数组字符串，如 `"[0x0000,0x2710]"`。

### 3. 生成标准节点（遵循现有脚本格式）

#### 3.1 写寄存器 — `10_设置：`

```json
{
    "function": "writeMultipleRegisters",
    "id": "<序号>",
    "inputs": {
        "address": "0xA009",
        "dataFormat": "registers",
        "deviceId": "MODBUS1",
        "unitId": "0x0F",
        "values": "[0x0000,0x0E10]"
    },
    "kind": "action",
    "moduleId": "device",
    "name": "10_设置：输入过压点（0x0F,0xA009,3600）",
    "timeout": { "timeoutMs": 3000 }
}
```

- 文本写入（资产码/SN 等）：`dataFormat: asciiText/utf8Text`，加 `byteOrder`、`registerCount`、`padByte`，`values` 换成 `text`。

#### 3.2 读寄存器 — `03_查询：`

```json
{
    "function": "readHoldingRegisters",
    "id": "<序号>",
    "inputs": {
        "address": "0xA009",
        "count": 2,
        "deviceId": "MODBUS1",
        "unitId": "0x0F"
    },
    "kind": "action",
    "moduleId": "device",
    "name": "03_查询：输入过压点（0x0F,0xA009,2）",
    "timeout": { "timeoutMs": 3000 }
}
```

#### 3.3 解析方式自动选择（不需要用户逐条说明）

**代理自己判断用哪种解析，判断依据是寄存器表条目 + 用户措辞，优先级从高到低：**

1. **协议表写 "按bit生效 / 每两个bit / Bit0-1 / ByteN BitN / 1为..."** → 位解析 `decodeBinary`（如 `0xA411` 全自动结果、`0xA415` 通讯状态、`0xA42B` 模块地址测试结果）。
2. **协议表写缩放/偏移（"×10V"、"0.1度/bit"、"偏移50℃"、"0~100"）或条目是纯数值量** → 直接解析 `decodeRegisters`（如 `0xA009` 过压值、温度、PWM）。
3. **协议表类型是 `char[N]` / `CHAR[N]`** → 文本解析 `decodeRegisters + utf8Text/asciiText`（如资产码、编译时间）。
4. **条目写 "0-不保护 1-保护 / 0-断开 1-闭合 / 状态值表（0:空闲 1:检测中…）"** → 先 `decodeRegisters` 拿整值，再用 `equal` 对枚举值判定（如 `0xA410` 全自动阶段 == 100）。
5. **协议表没有该地址/位定义** → 询问用户或按描述措辞判断（出现 "第几位/bit/位" 字样走位解析），并标注"待实机确认"。

**用户描述措辞的补充信号：**
- 说"第 N 位 / bit / 状态位" → 位解析。
- 说"值 / 数值 / 设置成多少" → 直接解析。
- 只给地址不给意图时，先按协议表类型判断，生成后在输出里注明"我按 X 方式解析，依据是协议表 Y"。

**生成时在输出中声明解析方式与依据**，用户一句话即可纠正，不必每次都重新解释规则。

#### 3.4 解析节点示例 — `解析xxx`（`builtin.data-parser`）

- 数值：`decodeRegisters`（`dataType`、`layout`、`registerOffset`、`scale`、`valueOffset`）。
- 位提取：`decodeBinary`（`unit: bit/byte`、`offset`、`length`、`bitOrder: lsb0`、`byteOrder`）。
- 文本：`decodeRegisters` + `dataType: utf8Text` + `padding: trimTrailingNulls`。
- `registerOffset` 是返回寄存器数组的零基索引；`source` 引用上游步骤输出，如 `${step:011.032.011.outputs.registers}`。

#### 3.5 判定 — `Limit Check` / `判断xxx`

```json
{
    "id": "<序号>",
    "inputs": { "actual": "${step:...outputs.value}" },
    "kind": "limit",
    "name": "Limit Check",
    "parameters": {
        "comparison": "equal",
        "expected": 360,
        "measurementName": "Measurement",
        "unit": "V",
        "tolerance": 0
    }
}
```

- `equal` + `expected`；`between` + `lower/upper`（可 `inclusive`）；`greaterOrEqual`/`lessOrEqual`；`isTrue`/`isFalse`。
- 引用必须指向当前执行分支的节点，嵌套用完整路径（如 `041.042.045`）。

#### 3.6 配套节点

- 等待：`kind: wait` + `ms`（写后回读前一般 20ms，状态变化等待按需 500ms/10s）。
- 测试项包裹：连续步骤放入 `kind: testItem`，配 `retry.maxAttempts`（写读回校验 3 次；监控状态变化 20~100 次）。
- 人工确认：`kind: operatorPrompt`，`mode: notice`（提示后继续）或 `judgment`（操作员 PASS/FAIL）。

### 4. 命名规范（与现有脚本一致）

| 动作 | 命名 |
|------|------|
| 写 | `10_设置：<含义>（<unitId>,<地址>,<值/长度>）` |
| 读 | `03_查询：<含义>（<unitId>,<地址>,<count>）` |
| 解析 | `解析<含义>` / `获取<含义>寄存器` / `提取<含义>位` |
| 判定 | `判断<含义>` / `检查<含义>` / `Limit Check` |
| 等待 | `Wait 20ms` / `Wait 10s` |

地址在名称中保持十六进制大写（`0xA009`），值用十进制或十六进制按现有习惯。

### 5. 完整模板：写 → 回读 → 解析 → Limit

```text
testItem（如"设置输入过压点"，retry 3）
├── 10_设置：输入过压点（0x0F,0xA009,3600）   writeMultipleRegisters [0x0000,0x0E10]
├── Wait 20ms
├── 03_查询：输入过压点（0x0F,0xA009,2）      readHoldingRegisters count=2
├── 解析输入过压点                            decodeRegisters uint32 scale=0.1
└── Limit Check                               equal expected=360 unit=V
```

- 每次 `10_设置` 之后必须 `03_查询` 回读验证（FC10 回包 PASS ≠ 参数生效）。
- 状态类监控（急停、门禁、水浸）用 testItem + retry 轮询，等待操作员动作后状态位翻转。

### 6. 输出

生成 JSON 时：
- 保持 `groups: setup/main/cleanup` 骨架，`id` 用三位数字（`001`、`002`…），子步骤用两位（`01`、`02`…），与现有脚本一致。
- 输出完整可编译的 sequence JSON 片段；同时给出"我做了什么换算"的说明（地址来源、scale、位偏移计算过程），便于用户复核。
- 涉及协议表未定义内容，明确标注"待实机确认"。

## 安全边界

- 不修改用户脚本，除非明确要求；先给出方案与换算说明，确认后写入。
- 不把写命令成功当作参数生效；必须有回读或实测证据。
- 任何可能改变设备状态的实机写入前，明确提示写入地址、值和恢复方式。
- 生成脚本后建议用引擎编译一次（或提示用户在 UI 中 Compile），再上实机。
- RTU 实机响应与通用 Modbus 规范不一致时，记录原始请求/响应、CRC 校验结果和实际返回范围；不要在没有帧证据时修改脚本或引擎。
