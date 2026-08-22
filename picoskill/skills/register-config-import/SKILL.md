---
name: register-config-import
description: 把机型寄存器配置表（Excel/表格/口述）转换为 PicoATE 三层 TestItem JSON 配置步骤，不依赖小工具。负责解析"起始地址/参数名称/数据类型/寄存器数/功能/寄存器值/偏移/缩放比例"列，按分页生成"寄存器表配置写入"最外层 TestItem，W/RW 生成写+回读校验、R 生成只读校验。Use when converting a register config sheet into sequence JSON steps.
---

# PicoATE 寄存器表配置导入

## 目标

把机型寄存器配置表转换为可插入 sequence 的三层 TestItem JSON，**不依赖任何小工具**（代理直接读表/听描述生成）：

```text
寄存器配置表 → 分页分组 → 参数换算 → 三层 TestItem JSON → 插入 sequence Main 开头
```

## 输入格式（标准配置表）

Excel/表格列（按表头识别，顺序可不同）：

| 列 | 说明 |
|---|---|
| 起始地址 | 十六进制，如 `0xA001` |
| 参数名称 | 如 `堆功率`、`最高允许电压` |
| 数据类型 | `U16`/`U32`/`U64`/`int` |
| 寄存器数 | 1/2/4（可据类型推导） |
| 功能 | `R/W`、`R`（决定写不写） |
| 寄存器值 | **物理值**（如 2500） |
| 偏移 | 可空（valueOffset） |
| 缩放比例 | 可空（scale），如 10 表示 ×10 |

典型分页（sheet）：M2系统监控(0xA000)、CCU终端共用(0x6000)、MCU主控板(0x0200)。

## 生成结构（三层嵌套）

```text
寄存器表配置写入（最外层 TestItem，顶层 id 取空闲值）
└── 分页 TestItem（按 sheet/地址段，id 01/02/03...）
    └── 参数 TestItem（每个寄存器参数，id 01/02...）
        ├── W/RW: 01写 → 02Wait20ms → 03读 → 04解析 → 05Limit
        └── R:    01读 → 02解析 → 03Limit
```

## 换算规则

- **写寄存器值 = (配置值 - 偏移) / 缩放比例**（配置表的"寄存器值"是物理值）
  - 例：4000 / scale=10 → 写 `0x0190`(400)
- U16=1 寄存器：`[0xNNNN]`
- U32=2 寄存器：高字在前 `[0xHHHH,0xLLLL]`
- U64=4 寄存器：`[0xA,0xB,0xC,0xD]`（高到低）
- 解析步骤：`decodeRegisters` 用 `scale`/`valueOffset` 还原物理值
- Limit：`expected` = 配置值（物理值），`comparison: equal`

## 分页 unitId 约定

| 分页 | unitId |
|---|---|
| M2系统监控 | `0x0F` |
| CCU终端共用 | `0x01` |
| MCU主控板 | `0x00` |
| 其他 | 默认 `0x00` |

## 参数 TestItem 模板（W/RW）

```json
{
    "id": "01",
    "kind": "testItem",
    "name": "堆功率",
    "retry": { "maxAttempts": 3 },
    "steps": [
        { "function": "writeMultipleRegisters", "id": "01",
          "inputs": { "address": "0xA001", "dataFormat": "registers",
                      "deviceId": "MODBUS1", "unitId": "0x0F",
                      "values": "[0x0000,0x09C4]" },
          "kind": "action", "moduleId": "device",
          "name": "10_设置：堆功率（0x0F,0xA001,2500）",
          "timeout": { "timeoutMs": 3000 } },
        { "id": "02", "kind": "wait", "ms": 20, "name": "Wait 20ms" },
        { "function": "readHoldingRegisters", "id": "03",
          "inputs": { "address": "0xA001", "count": 2,
                      "deviceId": "MODBUS1", "unitId": "0x0F" },
          "kind": "action", "moduleId": "device",
          "name": "03_查询：堆功率（0x0F,0xA001,2）",
          "timeout": { "timeoutMs": 3000 } },
        { "function": "decodeRegisters", "id": "04",
          "inputs": { "dataType": "uint32", "layout": "normal",
                      "registerOffset": 0, "scale": 1,
                      "source": "${step:<路径>.03.outputs.registers}",
                      "valueOffset": 0 },
          "kind": "action", "moduleId": "builtin.data-parser",
          "name": "解析堆功率" },
        { "id": "05", "kind": "limit",
          "inputs": { "actual": "${step:<路径>.04.outputs.value}" },
          "name": "Limit Check",
          "parameters": { "comparison": "equal", "expected": 2500,
                          "measurementName": "Measurement", "tolerance": 0 } }
    ]
}
```

R 参数：去掉写和 wait，读=01、解析=02、Limit=03，引用路径相应为 `.01`/`.02`。

## 插入现有 sequence（关键步骤）

1. **生成配置 TestItem** 后，顶层 id 必须换成**当前 sequence 空闲的 id**（避免与已有 testItem 冲突，如 001/002/003 已占用则用 006）。
2. **内部引用同步**：`${step:001.` → `${step:<新id>.`（递归替换所有 `${step:001.` 前缀）。
3. **插入位置**：Main 组最开头（第一个 testItem 之前），保持原流程顺序不变。
4. **验证**：
   - 顶层 id 全局唯一（跨 group）
   - `${step:XXX.YY.ZZ.WW` 引用的节点存在
   - 引用路径 = 根id.分页id.参数id.子步骤id

## 完整实操示例（插入现有 sequence）

以 `terminal_main_sequence.json`（main 原 37 步）为例：

**① 确定新顶层 id**

现有顶层 id 全集（跨 group）：`001/002/003/007/008/009/010~044`。
取**最小空闲** `006`（004/005 留给未来）。

**② 生成配置 TestItem 并改 id**

工具生成物顶层 `id=001`，改 `006`；同时**递归替换内部所有 `${step:001.` → `${step:006.`**（只动配置块自己的引用，不影响 setup 里已有的 `${step:001.04...}`）。

**③ 插入 main 开头**

```text
插入前: main = [008 确认终端上电, 010 关门急停, ...]   (37 步)
插入后: main = [006 寄存器表配置写入, 008 确认终端上电, 010 关门急停, ...]  (38 步)
```

**④ 验证结果**

```
✓ JSON 有效
✓ main steps 37 → 38，第 1 个是 [006] 寄存器表配置写入，第 2 个 [008] 原流程不变
✓ 顶层 id 唯一（006 不与任何现有 id 冲突）
✓ 配置块引用 ${step:006.01.01.03.outputs.registers} 指向存在节点
```

**⑤ 结构样例（合并后）**

```json
{
    "id": "006",
    "kind": "testItem",
    "name": "寄存器表配置写入",
    "steps": [
        { "id": "01", "kind": "testItem", "name": "M2系统监控(0xA000-0xD7FF)",
          "steps": [
              { "id": "01", "kind": "testItem", "name": "堆功率",
                "steps": [
                    { "function": "writeMultipleRegisters", "id": "01",
                      "inputs": { "address": "0xA001", "values": "[0x0000,0x09C4]",
                                  "deviceId": "MODBUS1", "unitId": "0x0F" },
                      "kind": "action", "moduleId": "device",
                      "name": "10_设置：堆功率（0x0F,0xA001,2500）" },
                    { "id": "02", "kind": "wait", "ms": 20 },
                    { "function": "readHoldingRegisters", "id": "03",
                      "inputs": { "address": "0xA001", "count": 2,
                                  "deviceId": "MODBUS1", "unitId": "0x0F" },
                      "kind": "action", "moduleId": "device",
                      "name": "03_查询：堆功率（0x0F,0xA001,2）" },
                    { "function": "decodeRegisters", "id": "04",
                      "inputs": { "dataType": "uint32", "scale": 1,
                                  "source": "${step:006.01.01.03.outputs.registers}" },
                      "kind": "action", "moduleId": "builtin.data-parser",
                      "name": "解析堆功率" },
                    { "id": "05", "kind": "limit",
                      "inputs": { "actual": "${step:006.01.01.04.outputs.value}" },
                      "parameters": { "comparison": "equal", "expected": 2500,
                                      "measurementName": "Measurement", "tolerance": 0 } }
                ] }
          ] }
    ]
}
```

**注意**：
- 引用路径 `006.01.01.03` = 根(006).分页(01).参数(01).子步骤(03=读)；limit 用 `.04`（解析）。
- R 参数子步骤是 读01/解析02/limit03，引用路径相应为 `.01`/`.02`。
- 若 sequence 有 `variables` 顶层字段，合并时**保留不动**（不要覆盖用户变量）。

## 命名习惯

- 最外层：`寄存器表配置写入`
- 分页：直接用 sheet 名（如 `M2系统监控(0xA000-0xD7FF)`）
- 参数：直接用参数名称
- 步骤：`10_设置：<名>（unitId,addr,值）`、`03_查询：<名>（unitId,addr,count）`、`解析<名>`、`Limit Check`

## 验证清单

- [ ] 每个分页 unitId 正确（M2=0x0F/CCU=0x01/MCU=0x00）
- [ ] 写值 = (物理值-偏移)/scale，U32 高字在前
- [ ] W/RW 有 5 步（写/wait/读/解析/limit），R 有 3 步（读/解析/limit）
- [ ] 引用路径是完整嵌套路径，指向存在的节点
- [ ] 顶层 id 与 sequence 现有 id 不冲突
- [ ] Limit expected = 配置物理值

## 安全边界

- 只读分析/生成；插入用户脚本前先说明改动（新 id、插入位置），等待授权。
- 配置表列名/含义不明确时先确认，不臆测缩放。
- 生成的配置步骤建议先用引擎 Compile 再上实机。
