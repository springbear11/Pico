---
name: interaction-patterns
description: PicoATE 测试脚本的人机交互使用习惯规范。定义"人工动作+状态位轮询+人工判断框"的固定模式：需要人工动作的先发 notice 提示，能查状态位证明生效的用 testItem 轮询（retry 50 次/500ms），必须人眼/人耳判断的再加 judgment 判断框；纯状态位可验证的省略判断框。Use when designing operator-facing test steps (prompt, polling, confirmation).
---

# PicoATE 人机交互模式（使用习惯）

## 目标

统一"操作员交互 + 状态位验证"的脚本写法，让需要人工参与测试项的结构一致、可维护：

```text
人工动作 → notice 提示 → 状态位轮询证明 → （必要时）judgment 人工判断 → 恢复动作
```

## 决策规则（按优先级）

| 场景 | 做法 |
|---|---|
| 需要操作员**做物理动作**（关门、按急停、短接） | 先发 `notice` 提示，再**轮询状态位**证明动作生效 |
| 动作有**状态位可查**（门禁、急停、接触器、锁） | 用 testItem 轮询状态位即可，**不加判断框** |
| 结果**必须人眼/人耳判断**（灯带颜色、风扇声音、指示灯） | 轮询状态位之后（或代替）加 `judgment` 判断框 |
| 纯状态位可验证、无人工判断需求 | **省掉 judgment 弹窗**，只轮询 |

一句话：**能查到状态位的用轮询，查不到的靠人，人看的东西用 judgment。**

## 标准结构

### 模式 A：人工动作 + 状态位确认（无 judgment）

```json
{
    "id": "010",
    "kind": "testItem",
    "name": "终端关门急停测试",
    "steps": [
        {
            "id": "045",
            "kind": "operatorPrompt",
            "name": "关闭防护门、按下急停",
            "prompt": {
                "message": "请关闭终端${var.UUT_UnitID} 防护门、并按下急停键1秒",
                "mode": "notice",
                "closeOnStep": "010.046",
                "timeoutMs": 60000,
                "title": "Message"
            }
        },
        {
            "id": "046",
            "kind": "testItem",
            "name": "检查防护门关闭、急停键按下",
            "retry": { "maxAttempts": 50, "delayMs": 500 },
            "steps": [
                { "function": "readHoldingRegisters", "id": "02",
                  "inputs": { "address": "0x644A", "count": 2, "deviceId": "MODBUS1", "unitId": "0x0N" },
                  "kind": "action", "moduleId": "device", "name": "03_查询：门禁状态（关门）" },
                { "function": "decodeRegisters", "id": "03",
                  "inputs": { "dataType": "uint32", "layout": "normal", "registerOffset": 0, "scale": 1,
                              "source": "${step:010.046.02.outputs.registers}", "valueOffset": 0 },
                  "kind": "action", "moduleId": "builtin.data-parser", "name": "解析门禁状态" },
                { "id": "04", "kind": "limit",
                  "inputs": { "actual": "${step:010.046.03.outputs.value}" },
                  "name": "检查门禁状态为关门（0）",
                  "parameters": { "comparison": "equal", "expected": 0, "measurementName": "Measurement", "tolerance": 0 } }
            ]
        }
    ]
}
```

### 模式 B：动作 + 状态位轮询 + 人工判断

```json
{
    "id": "011",
    "kind": "testItem",
    "name": "终端风扇测试",
    "retry": { "maxAttempts": 3 },
    "steps": [
        { "id": "045", "kind": "operatorPrompt",
          "name": "终端风扇启动",
          "prompt": { "message": "请确认终端风扇将要启动", "mode": "notice",
                      "closeOnStep": "011.07", "timeoutMs": 60000, "title": "Message" } },
        { "function": "writeMultipleRegisters", "id": "01",
          "inputs": { "address": "0x6811", "dataFormat": "registers", "deviceId": "MODBUS1",
                      "unitId": "0x0N", "values": "[0x0032]" },
          "kind": "action", "moduleId": "device", "name": "10_设置：终端风扇速度50" },
        { "id": "07", "kind": "testItem", "name": "检查门禁（打开）",
          "retry": { "maxAttempts": 50, "delayMs": 500 },
          "steps": [ /* 读状态位 → 解析 → limit 检查动作生效 */ ] },
        { "id": "02", "kind": "operatorPrompt",
          "name": "确认风扇启动",
          "prompt": { "message": "确认：终端风扇是否正常启动", "mode": "judgment",
                      "passText": "PASS", "failText": "FAIL",
                      "failureCode": "OperatorCheckFailed",
                      "timeoutMs": 60000, "title": "Message" } },
        { "function": "writeMultipleRegisters", "id": "03",
          "inputs": { "address": "0x6811", "dataFormat": "registers", "deviceId": "MODBUS1",
                      "unitId": "0x0N", "values": "[0x0000]" },
          "kind": "action", "moduleId": "device", "name": "10_设置：终端风扇停止" }
    ]
}
```

### 模式 C：纯人工观察（无状态位可查）

```json
{
    "id": "012",
    "kind": "testItem",
    "name": "终端灯带测试",
    "steps": [
        { "id": "01", "kind": "operatorPrompt",
          "name": "检查灯带状态",
          "prompt": { "message": "请观察终端灯带颜色变化", "mode": "notice",
                      "closeOnStep": "012.045", "timeoutMs": 60000, "title": "Message" } },
        { "id": "05", "kind": "loop", /* 灯带循环闪烁（for 循环驱动颜色切换） */ },
        { "id": "045", "kind": "operatorPrompt",
          "name": "确认灯带状态",
          "prompt": { "message": "主机灯带是否按红-绿-蓝循环闪烁", "mode": "judgment",
                      "passText": "PASS", "failText": "FAIL",
                      "failureCode": "OperatorCheckFailed",
                      "timeoutMs": 60000, "title": "Message" } },
        { "function": "writeMultipleRegisters", "id": "046",
          "inputs": { "address": "0x6814", "dataFormat": "registers", "deviceId": "MODBUS1",
                      "unitId": "0x0N", "values": "[0x0000,0x0003]" },
          "kind": "action", "moduleId": "device", "name": "10_设置：恢复灯带默认状态" }
    ]
}
```

## 固定参数约定

| 元素 | 约定 |
|---|---|
| **状态位轮询** | testItem + `retry.maxAttempts=50` + `retry.delayMs=500`（50 次 × 500ms ≈ 25s 等待窗口） |
| **notice 提示** | `mode: notice`，`closeOnStep` 指向后续轮询/判断项，`timeoutMs: 60000` |
| **judgment 判断框** | `mode: judgment`，`passText: PASS`，`failText: FAIL`，`failureCode: OperatorCheckFailed` |
| **收尾恢复** | 判断之后用 `10_设置` 写恢复默认状态，不遗漏 |
| **轮询内引用** | 完整路径：`${step:<父>.<子>.02.outputs.registers}` → decode → `${step:<父>.<子>.03.outputs.value}` → limit |

## 命名习惯

- 提示类：`关闭防护门、按下急停` / `终端风扇启动` / `检查灯带状态`
- 轮询类：`检查<对象><状态>`（如 `检查防护门关闭、急停键按下`、`检查门禁（打开）`）
- 判断类：`确认<对象>`（如 `确认风扇启动`、`确认灯带状态`）
- 写动作：`10_设置：<对象><动作>`；查询：`03_查询：<对象><状态>`；解析：`解析<对象>`

## 判断框 vs 轮询的选择

1. **优先轮询状态位**：动作有寄存器反馈（门禁 0x644A、急停 0x6448、锁 0x6410、接触器 0x6414）→ 只轮询，不加判断框。
2. **轮询 + 判断框**：状态位证明"动作已生效"，但结果质量（转速是否正常、颜色是否正确）仍需人确认。
3. **纯判断框**：无状态位（灯带颜色、指示灯、外观）→ notice + judgment。
4. **轮询超时兜底**：50 次轮询未通过 → 测试 FAIL（无需判断框），操作员没做动作时不会卡死。

## 安全边界

- notice 只是"提示后继续"，**不保证操作员真的执行了动作**——所以必须靠后续状态位轮询兜底，不能只发提示就 PASS。
- judgment 需要操作员点击，适合"必须人工确认"的场景；能自动化验证的不要滥用判断框（增加产线节拍和误操作风险）。
- 恢复动作放在判断之后且必须执行，避免设备停留在测试态。
