---
name: pico-log-analysis
description: 分析 PicoATE/ATE 自动测试日志，结合 Modbus 协议寄存器表、sequence JSON 和执行引擎语义定位通信失败、地址/寄存器偏移错误、引用错误、解析异常、测不过原因和假 PASS。Use when Codex needs to inspect UUT TXT/CSV logs, Modbus TCP frames, register maps, sequence files, alarm bitmaps, cleanup/alwaysRun behavior, or produce a graded test audit.
---

# PicoATE 日志分析

## 目标

使用原始日志、测试序列和寄存器表建立可追溯证据链：

```text
测试步骤 → TX/RX 原始帧 → 寄存器/字节/位解析 → Limit 判定 → 最终结果
```

不要只依据 `RESULT:PASS` 或人工确认判断功能正确；必须区分通信成功、解析成功、测量判定成功和真实硬件状态变化。

## 输入与保护

优先收集：

- UUT `.txt` 日志，以及同名 `.csv` 报告；
- 实际执行的 sequence `.json`；
- 对应版本的寄存器映射表 `.md`；
- 如果涉及插件错误，收集插件 DLL/源码版本和 Station 配置。

先确认文件版本、时间戳、路径和 Git 工作区状态。分析阶段只读，不修改用户的 `debug`、Station、路由、测试脚本或日志文件；需要修复时先单独说明并等待授权。

## 标准流程

### 1. 建立步骤索引

按步骤名称、ID、TestItem 层级和 `ATTEMPT n/m` 建立索引。记录每个测试块的：

- 设置动作和查询动作；
- `deviceId`、`unitId`、功能码、地址、数量；
- `registerOffset`、`offset`、`length`、`unit`、`bitOrder`、`byteOrder`；
- `scale`、`valueOffset`、数据类型和 Limit 期望值；
- 引用表达式，例如 `${step:...outputs.registers}`；
- retry、onFail、alwaysRun 和 cleanup 归属。

检查引用是否指向当前分支、当前 TestItem 和同一次执行，而不是同 ID 的旧节点或其他分支节点。

### 2. 校验 Modbus TCP 帧

响应帧按以下结构拆分：

```text
Transaction ID(2) Protocol ID(2) Length(2) Unit ID(1) Function(1) Data
```

FC03/FC04 响应的 Data 从 Byte Count 后开始；FC10 正常响应只确认写入地址和数量，不等于实际参数已经正确生效。

逐项核对：

- TX/RX transaction ID 是否一致；
- unit ID 是否符合寄存器表和设备实际链路；
- function 是否正确；
- FC03/FC04 的字节数是否为 `count × 2`；
- FC10 响应地址和数量是否与请求一致；
- 是否出现 Modbus exception、插件错误或响应截断。

### 3. 从原始字节重建寄存器

对 FC03/FC04，先去掉 MBAP、Unit ID、Function、Byte Count，再按两个字节组成寄存器：

```text
数据区：00 00 02 C7
寄存器：[0x0000, 0x02C7]
```

`registerOffset` 是解析器输入数组的零基索引，不是设备绝对地址：

```text
registerOffset=1 → 取第二个返回寄存器
```

`decodeRegisters` 的 `rawHex` 是选中寄存器经过 `layout` 后的字节；`normal` 通常按 ABCD 解释，不能把 `registerOffset` 当成字节偏移。

### 4. 解释二进制字段

`decodeBinary` 默认使用零基偏移：

- `unit=byte`：`offset=0` 是第一个字节；
- `unit=bit`：`offset=0` 是第一个字节的 Bit0；
- `bitOrder=lsb0`：每个字节最右侧位为 Bit0，向左依次为 Bit1、Bit2；
- `byteOrder=big`：对多字节数按大端处理。

例如：

```text
0x02 = 0000 0010
位号    7654 3210
Bit1=1，Bit0=0
```

不要把“从左往右数第几位”和协议的 LSB 位号混用。分析时同时写出：原始字节、位号约定、配置 offset、解析结果。

对于完整 `U32`：

```text
Byte1 Byte2 Byte3 Byte4
 64    61    CF    00
```

Byte1 也是 U32 的最高有效字节/HSB（或 MSB），但不能仅凭它在第一位就断定它是某个业务字段；必须以协议表和对比实验确认字段含义。

### 5. 对照寄存器表

优先以用户指定的寄存器表版本为准，记录表名和相关条目。常见检查：

- 配置值单位：欠压/过压通常按 `0.1V/bit` 或协议表声明的缩放；
- 温度的 `valueOffset` 是否符合表定义，例如原始值减 `50`；
- 主机状态 `0xA400~0xA401`、`0xA402~0xA403` 的字节和位定义；
- 告警位图 `0x3B10~0x3B2F` 的字节编号、Unit ID 和位图分区；
- 全自动结果 `0xA411~0xA412` 每两个 bit 一个状态；
- 写入控制寄存器与只读反馈寄存器是否被混用；
- 协议表没有定义的位，不能仅凭 PASS 认定测试有效。

已知的常用换算示例（仍需以当前协议版本复核）：

- `0xA007~0xA008`：输入欠压阈值；
- `0xA009~0xA00A`：输入过压阈值；
- `0xA400~0xA401`：主机状态1；常见 Byte1 Bit7 为水浸、Byte2 Bit6/Bit7 为门禁1/2、Byte3 Bit0 为门禁3；
- `0xA402~0xA403`：主机状态2；常见 Byte1 Bit0~Bit2 为门禁4~6、Byte2 Bit0~Bit2 为断上级/急停/液位；
- `0xA411~0xA412`：全自动测试结果，每两个 bit 表示未测试/通过/失败/测试中。

### 6. 分析失败原因

**先确认「这个步骤是否真的失败了」再下结论，这是最常见的误判来源：**

1. **区分「轮询等待失败」和「最终失败」**：带 `retry` 的监控型 TestItem（如急停、水浸、门禁等待操作员动作）每次 attempt 读到的状态未满足是正常现象，**必须找到最后一次 attempt 的结果**，不能因为 grep 到大量 `RESULT:FAIL` 就判定测试失败。本次教训：急停监控重试 24 次读到 0，第 25 次操作员按下急停后读到 1 并通过——只匹配 FAIL 行会把 PASS 的测试误判为失败。
2. **先看 TestItem 的 `_TESTITEM_END` 与整体 `RESULT`**，再回头看中间 attempt。一个 TestItem 最终 `RESULT:PASS` 时，中间的 FAIL 只是等待过程。
3. **grep 检索失败证据时，同时检索通过证据**（例如同时搜 `判断急停位为1` 与 `LIMIT_RESULT PASS`），避免只看到失败侧。
4. **分析人工确认型步骤时，先确认操作员动作是否真的发生**：`notice` 模式 PROMPT 会自动 PASS，不保证操作员执行了动作；若重试期间寄存器从未变化，可能是操作员没动作，而不是脚本错误。

按证据强度分层：

1. **通信层**：无响应、异常帧、Unit ID 错、地址/数量错误、插件错误；
2. **原始数据层**：设备返回值未变化、返回固定零、动作前后帧相同；
3. **解析层**：引用错、寄存器偏移错、字节序错、bitOrder 错、scale/valueOffset 错；
4. **判定层**：期望值/上下限/有效电平错误；
5. **测试流程层**：没有实际施加刺激、等待过短、恢复动作未执行、重试只重复读取；
6. **人工确认层**：操作员 PASS 不能证明寄存器反馈变化。

每个结论都要引用至少一组原始 TX/RX、解析日志和 Limit 日志；没有证据时标记为“待实机复测”，不要猜测。

### 7. 筛选假 PASS

重点检查：

- FC10 回包 PASS 后没有 FC03 回读；
- 只验证写命令成功，没有验证参数实际生效；
- 操作员 Prompt PASS 被当成寄存器反馈；
- 预期值为 `0`，但设备一直返回默认零或固定值；
- 读错 Unit ID、告警图或预留位；
- 多个传感器返回完全相同默认值，统计范围仍能 PASS；
- 测试只检查通信步骤 PASS，没有检查 Limit；
- `alwaysRun` 恢复动作属于 Main，但前置失败后仍受 `OnSuccess` 依赖阻塞；
- cleanup 未执行或执行结果未进入最终报告；
- 未选择的全自动测试项没有验证为“未测试”。

对于每个疑似假 PASS，说明“当前 PASS 证明了什么”和“还没有证明什么”。

### 8. 分析 alwaysRun 与 cleanup

区分两个层级：

- `alwaysRun`：测试项内部的补偿动作，例如恢复默认过压/欠压阈值、关闭临时输出；
- `cleanup`：整个 Session 的资源清理，例如关闭 Modbus TCP、停止周期任务、释放资源。

当 Main 中某节点失败后：

```text
普通后续节点应跳过
alwaysRun 补偿动作应执行
Main 完成后才进入全局 cleanup
```

如果 alwaysRun 被保留但仍挂在失败节点的 `OnSuccess` 串行依赖上，它会既不被跳过也无法执行，导致 Main 不完成，cleanup 看起来像“没有运行”。分析时检查执行引擎是否对 alwaysRun 前置边使用 `Finally` 语义；不要把所有普通 Main 边全局改成 Finally。

Cleanup 在没有任何设备 TX/RX 之前失败时，优先判断为表达式/生命周期依赖错误，而非通信错误。例如 `RuntimeVariableResolutionError | StepResultUnavailable: Referenced step has not produced a result: 006` 表示 Cleanup 节点引用的结果在当前结果帧不可用，恢复写根本没有发出。先检查序列结构：普通设备生命周期应为 `Setup=open`、`Cleanup=close`；Cleanup 不应依赖 `${step:...}` 的跨阶段结果，除非已明确验证该会话的结果持久化语义。修复脚本即可，不要据此修改引擎。

## 输出格式

默认用简洁中文输出，先给一句现象，再给分级结论：

```text
现象：...

P0 明确错误
- 步骤/文件：...
- 证据：TX/RX、解析、Limit
- 影响：...

P1 测不过原因
- ...

P1 假 PASS 风险
- 当前 PASS 证明：...
- 尚未证明：...

建议修改顺序
1. ...
2. ...

待复测项
- 指定动作、读取地址、期望原始帧变化和判定结果。
```

用户要求“一句话”时只保留现象或结论，不拆段；用户要求“不改文件”时只分析，不执行修改。

## 安全边界

- 不修改用户日志、测试序列、Station、路由或 debug 脚本，除非用户明确要求；
- 不把旧日志当成新 JSON 的验证证据；
- 不把 `RESULT:PASS` 当成业务功能 PASS；
- 不把协议表未定义的偏移当成确定事实；
- 运行复现脚本时使用独立临时文件，完成后清理；
- 任何可能改变设备状态的实机写入前，明确提示写入地址、值和恢复方式。
