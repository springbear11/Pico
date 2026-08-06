# PicoATE Sequence JSON Schema

This document describes the JSON shape currently accepted by
`SequenceCompiler`.

The compiler treats missing optional fields as defaults. If a known field is
present with the wrong JSON type, compilation fails with a path-specific error.

## Sequence Object

| Field | Type | Required | Default | Notes |
|-------|------|----------|---------|-------|
| `id` | string | yes | empty | Stable sequence id. |
| `name` | string | yes | empty | Human-readable name. |
| `version` | string | no | `0.1.0` | Used in generated plan id. |
| `metadata` | object | no | `{}` | Copied into `SequenceDef::metadata`. |
| `variables` | array | no | `[]` | 流程级类型化变量，可配置共享值或每个 UUT 的独立值。 |
| `moduleBindings` | array | no | `[]` | Runtime module transport bindings. |
| `groups` | array | yes | none | Array of group objects. |

## 序列变量

`variables` 保存在 Sequence JSON 根节点中，用来管理 CAN ID、MODBUS 地址、产品型号、
超时时间等随项目或 UUT 变化的测试参数。它不保存设备端口、VISA 地址或 CAN 设备 SN；
这些连接信息仍属于 `StationSystem.json`。

```json
{
  "variables": [
    {
      "name": "CAN_ID",
      "type": "hex",
      "scope": "perUut",
      "values": ["0x101", "0x102", "0x103", "0x104"],
      "description": "每个治具位置使用的 CAN ID"
    },
    {
      "name": "REQUEST_TIMEOUT_MS",
      "type": "integer",
      "scope": "shared",
      "value": 1500
    }
  ]
}
```

| Field | Type | Required | Notes |
|-------|------|----------|-------|
| `name` | string | yes | 变量名；只能使用字母、数字和下划线，首字符不能是数字。 |
| `type` | string | no | `string`、`integer`、`hex`、`double` 或 `bool`，默认 `string`。 |
| `scope` | string | no | `shared` 或 `perUut`，默认 `shared`。 |
| `value` | typed value | shared 时 yes | 所有 UUT 共用的值。 |
| `values` | array | perUut 时 yes | 按 UUT1、UUT2、UUT3、UUT4 顺序保存独立值。 |
| `description` | string | no | 给工程师看的用途说明，不参与执行。 |

Step 的 `inputs`、`parameters`、Limit 条件和 Loop 条件都可以通过统一表达式引用：

```json
{
  "inputs": {
    "canId": "${var.CAN_ID}",
    "timeoutMs": "${var.REQUEST_TIMEOUT_MS}"
  }
}
```

运行时由 Core 根据当前 UUT 自动选择 `values` 中对应位置：UUT1 使用 `values[0]`，
UUT2 使用 `values[1]`，以此类推。脚本中不要写 `CAN_ID[i]`，也不需要手工传入数组下标。
完整变量表达式会保留原始类型，因此 `hex`/`integer` 不会先变成普通字符串再交给插件。

当实际运行的 UUT 没有对应值、值为 `null`、类型不匹配、名称重复或名称非法时，
编译/运行准备会返回带变量路径的错误，不会退回 UUT1 的值。UI 与 CLI 调用同一个 Core
绑定函数，二者行为一致。当前 UUT 的内置信息还可使用 `${uut.index}`（从 0 开始）、
`${uut.number}` / `${uut.slot}`（从 1 开始）和 `${uut.id}`。
Flow 属性编辑器的 `fx -> Runtime Values` 可直接插入 `${var.serialNumber}`、`${uut.id}`、
`${frame.id}`、`${attempt.number}` 等内建表达式，不要求工程师记住或手写变量名。

完整示例见 `examples/sequence_variables.json`。

## Module Binding Object

`moduleBindings` connects action step `moduleId` values to runtime module
implementations. These bindings are not compiled into `ExecutionPlan`; they are
used by runtime setup to register modules on `ExecutionSession`.

每个启用的 binding 还会提供同名 DeviceSession Factory。例如 Station 设备配置
`driverId: "plugin.can.gcan"` 时，会复用 Sequence 中
`moduleId: "plugin.can.gcan"` 所声明的 Transport 来执行
`open/health/close` 和连接测试。

```json
{
  "moduleBindings": [
    {
      "moduleId": "external.echo",
      "transport": "qprocess",
      "program": "${PICOATE_MOCK_HOST}",
      "arguments": [],
      "timeoutMs": 3000
    }
  ]
}
```

| Field | Type | Required | Default | Notes |
|-------|------|----------|---------|-------|
| `moduleId` | string | yes | empty | Must match action step `moduleId`. |
| `transport` | string | no | `qprocess` | `qprocess` or `persistent-qprocess`. |
| `program` | string | yes when enabled | empty | Executable path or command name for the selected process transport. |
| `arguments` | array of string | no | `[]` | Arguments passed to the external process. |
| `timeoutMs` | number | no | `30000` | Per-call process timeout. |
| `enabled` | bool | no | `true` | Disabled bindings are ignored at runtime. |

`program` and `arguments` are resolved through `VariableResolver`. Supported
built-in placeholders:

```text
${SEQUENCE_DIR}
${PROJECT_DIR}
${PICOATE_MOCK_HOST}
${PICOATE_FAKE_INSTRUMENT_HOST}
${PICOATE_NATIVE_HOST}
${PICOATE_TEST_DLL}
${PYTHON_EXE}
```

Runtime code may provide additional variables through
`ModuleBindingRegistrationOptions::variables`; unresolved variables cause module
registration to fail before execution starts. Plain command names such as
`python` are left for `QProcess` to resolve through PATH. Relative paths with a
path separator are resolved relative to the sequence file directory.

The same resolver supports recursive replacement and nested container
replacement for future configuration files such as NativeHost manifests. See
`docs/变量与结果引用.md`.

NativeHost DLL module bindings should prefer `--manifest` arguments for DLL
load settings. The manifest format is documented in
`docs/NativeHost清单规范.md`.

## Group Object

### UI 标准流程骨架

Admin Flow Editor 将普通 ATE 项目的顶层流程固定为以下三段，三段即使暂时没有
Step 也必须保留：

```text
Setup -> Main -> Cleanup
```

- `Setup`：设备打开、工装初始化、上电和测试前准备。
- `Main`：普通 Step、TestItem、Loop、Limit 等正式测试内容。
- `Cleanup`：设备关闭、下电和现场恢复；失败、停止或异常时仍应执行。
- 打开旧脚本时，Flow Editor 会自动补齐缺失分组，并把文档标记为待保存。
- 这三个保留分组不能在属性页修改 `id`、`kind` 或启用状态；旧文件中的分组级
  `enabled` 会被移除。分组名称仍允许修改。
- `SequenceCompiler` 继续兼容历史脚本和底层测试，因此不会在 Core 层强制拒绝
  缺少某个分组的 JSON；“三段必备”是 UI 项目文档规范。

GCAN/创芯交叉回环示例已经按该规范整理：四个通道的 `open` 位于 Setup，两个
方向的收发与判定位于 Main，四个通道的 `close` 位于 Cleanup。

| Field | Type | Required | Default | Notes |
|-------|------|----------|---------|-------|
| `id` | string | no | JSON path | Stable group id. |
| `name` | string | no | `id` | Display name. |
| `kind` / `type` | string | no | `custom` | `setup`, `main`, `cleanup`, or `custom`. |
| `enabled` | bool | no | `true` | Core 兼容字段；UI 标准 Setup/Main/Cleanup 不提供分组级启停。 |
| `steps` | array | yes | none | Array of step objects. |

Execution order:

```text
Setup groups -> Body groups -> Cleanup groups
```

`main` and `custom` groups are both body groups and are bridged in the same
order they appear in the JSON. Empty or disabled groups do not create bridge
gaps. Cleanup groups are connected from the last non-empty normal group by a
`Finally` edge.

## Step Object

| Field | Type | Required | Default | Notes |
|-------|------|----------|---------|-------|
| `id` | string | yes for enabled steps | empty | Stable step id. Disabled steps are ignored by PlanBuilder. |
| `name` | string | no | `id` | Display name. |
| `kind` / `type` | string | no | `noop` | See step kinds below. |
| `enabled` | bool | no | `true` | Disabled steps are not compiled into the plan. |
| `alwaysRun` | bool | no | `false` | Cleanup groups and cleanup steps are always-run automatically. |
| `resultRecording` | bool | no | `true` | `true` writes this Step/TestItem row to CSV/XLSX; `false` hides only its own table row. TXT logs and overall pass/fail are unaffected. Child steps keep their own setting. |
| `checkpointBefore` | bool | no | `false` | Copied to `ExecNode::checkpointBefore`. |
| `checkpointAfter` | bool | no | `false` | Copied to `ExecNode::checkpointAfter`. |
| `parameters` | object | no | `{}` | Copied to `ExecNode::payload` for non-barrier steps. |
| `moduleId` | string | no | `mock.action` at runtime | Action module id used by `ActionNodeHandler`. |
| `function` | string | no | empty | Function name passed to the selected module. |
| `inputs` | object | no | `{}` | Input bindings passed to the selected module. Runtime variables are resolved before execution. |
| `ms` | number | no | none | Shortcut for wait steps; inserted into `parameters.ms`. |
| `resources` | array | no | `[]` | Array of resource requirement objects. |
| `retry` | object | no | default retry policy | See retry object. |
| `timeout` | object | no | default timeout policy | See timeout object. |
| `timeoutMs` | number | no | `0` | Shortcut for timeout. |
| `errorPolicy` | object | no | default stop policy | See error policy object. |
| `barrier` | object | no | inferred for barrier steps | See barrier object. |
| `loop` | object | no | default for-loop policy | Required for explicit loop configuration. |
| `prompt` | object | yes for operatorPrompt | none | 人工确认或条件关闭提示框配置。 |
| `steps` | array | yes for loop/testItem steps | none | Child steps for a loop body or composite test item. |
| `tags` | array of string | no | `[]` | Copied to `ExecNode::tags`. |

Supported step kinds:

| Value | Runtime mapping |
|-------|-----------------|
| `noop` | `ExecNodeKind::Noop` |
| `wait` | `ExecNodeKind::Wait` |
| `action` / `mockAction` | `ExecNodeKind::Action` |
| `barrier` | `ExecNodeKind::Barrier` |
| `cleanup` | `ExecNodeKind::Cleanup` |
| `loop` / `forLoop` | `ExecNodeKind::Loop` scheduler control node |
| `testItem` / `composite` | `ExecNodeKind::TestItem` aggregate control node |
| `limit` / `numericLimit` | `ExecNodeKind::Limit` 通用比较节点 |
| `operatorPrompt` / `prompt` | `ExecNodeKind::OperatorPrompt` 人机交互节点 |
| `statement` | 独立 `Statement` 节点；当前执行返回 `StatementNotImplemented` |
| `sequenceCall` | 独立 `SequenceCall` 节点；当前执行返回 `SequenceCallNotImplemented` |

Example with checkpoint flags:

```json
{
  "id": "measure",
  "name": "Measure",
  "kind": "action",
  "moduleId": "mock.measurement",
  "function": "measureVoltage",
  "inputs": {
    "outputs": {
      "actualVoltage": 4.999
    }
  },
  "checkpointBefore": true,
  "checkpointAfter": true
}
```

The same action module pattern is used in `examples/basic_sequence.json`.

Admin Flow Editor 中，插件 Action 的 `function` 不再作为普通自由文本输入，而是从
`PicoATE_Describe.functions` 生成下拉选项。设备类插件应使用 `moduleId: "device"`，并在
`inputs.deviceId` 中保存 Station 逻辑设备或通道，例如 `CAN1.CH1`；算法、文件处理等非设备插件
可以直接使用插件自己的 `moduleId`。UI 的目标设备筛选用于减少误配，最终 JSON 仍以上述字段为准。

## 人工提示节点

`operatorPrompt` 是引擎内置的人机交互节点，不调用业务 DLL。Core 只发布只读
RuntimeEvent 并等待 UI 回应，不依赖 Qt Widgets；TEST 和 Admin 使用同一个弹窗
Presenter，因此不会破坏 UI、调度器、业务插件三层解耦。

Flow Editor 对工程师只展示一个 `MessageBox` 基础功能。拖入时默认是人工确认，
在右侧 `Mode` 下拉框中可以切换为提示后继续或人工 PASS/FAIL 判断。`operatorPrompt`
只是 JSON 和 Core 内部名称，避免与 Qt 的 `QMessageBox` 类型混淆。

### 等待人工确认

弹窗出现后当前流程暂停，操作员必须用鼠标点击按钮才继续。Enter、扫码枪回车、
Esc 和标题栏关闭都不能误确认；Stop/Abort 仍可取消等待并进入既有 Cleanup 语义。

```json
{
  "id": "001",
  "kind": "operatorPrompt",
  "prompt": {
    "mode": "confirm",
    "title": "连接工装",
    "message": "请连接产品和工装，然后点击继续。",
    "image": "fixture_connection.png",
    "confirmText": "继续",
    "timeoutMs": 60000
  }
}
```

### 提示后继续执行

`notice` 弹窗显示后流程立即继续。`closeOnStep` 指定的 Step 最终执行结束时自动
关闭；不填写时默认使用图中的下一个正常 Step。结束结果可以是 Passed、Failed、
Error、Timeout 或 Cancelled。Retry 期间只代表一次 Attempt 结束，不会关闭弹窗；
只有该 Step 不再重试并进入最终状态后才关闭。

如果 `notice` 配置了 `dialogKey` 且没有配置 `closeOnStep`，弹窗不会在下一个 Step
结束时自动关闭。它会一直保留，供后续相同 `dialogKey` 的 `judgment` 节点原位更新。

Flow Editor 的 `Close after step` 使用可编辑下拉框：第一项是“下一启用 Step
（默认）”，其余候选只列出当前 MessageBox 之后且已启用的步骤。TestItem/Loop
子步骤使用完整作用域地址，例如 `002.checkButton`。下拉框仍允许手工输入高级地址；
目标被删除、失能或移动到 MessageBox 之前时，编译会明确报错，不会静默改用下一步。

```json
{
  "id": "002",
  "kind": "operatorPrompt",
  "prompt": {
    "mode": "notice",
    "title": "人工操作",
    "message": "请按住产品按键。",
    "closeOnStep": "003",
    "timeoutMs": 5000
  }
}
```

### 提示期间继续，结束后人工判定

需要操作员在自动测试期间持续观察灯光、声音或机构动作时，可以先放一个 `notice`，
让流程继续执行；自动步骤完成后，再放一个同 `dialogKey` 的 `judgment`。UI 会复用同一
窗口，不会先关闭再弹出另一个窗口。每个 UUT 的窗口独立，相同 Key 不会串到其他 UUT。

```json
{
  "id": "001",
  "kind": "operatorPrompt",
  "prompt": {
    "mode": "notice",
    "dialogKey": "rgb-lamp-check",
    "title": "指示灯观察",
    "message": "请观察红、绿、蓝指示灯是否依次点亮。"
  }
}
```

中间可以放普通 Step、TestItem 或 Loop。最后使用同一个 `dialogKey`：

```json
{
  "id": "010",
  "kind": "operatorPrompt",
  "prompt": {
    "mode": "judgment",
    "dialogKey": "rgb-lamp-check",
    "title": "确认指示灯结果",
    "message": "红、绿、蓝指示灯是否全部正常？",
    "passText": "PASS",
    "failText": "FAIL",
    "failureCode": "RgbLampOperatorFail",
    "timeoutMs": 60000
  }
}
```

操作员点击 PASS 时该 Step 返回 Passed；点击 FAIL 时返回 Failed，并使用
`failureCode` 作为错误码。后续停止还是继续，仍由 Station 的失败策略统一决定。

### 在弹窗中显示运行值

`message` 和 `title` 会在 MessageBox 执行前经过统一运行时变量解析。Flow Editor 的
Message 多行编辑框右侧提供 `fx` 菜单，可以在当前光标位置插入 SN、UUT、Attempt、
Sequence 变量或前序 Step 输出，不会覆盖已经写好的提示文字。

```json
{
  "id": "show-result",
  "kind": "operatorPrompt",
  "prompt": {
    "mode": "confirm",
    "title": "${uut.id} 测量结果",
    "message": "SN: ${sn}\n电压: ${step:read-voltage.outputs.value} V\n目标: ${var.targetVoltage} V",
    "confirmText": "确认",
    "timeoutMs": 60000
  }
}
```

嵌入文字中的值统一转换为可读字符串；布尔值显示为 `true/false`，数组和对象显示为
紧凑 JSON。前序 Step 引用会在编译期生成数据依赖边，并检查节点存在性和先后顺序。
运行时找不到变量、Step 未成功或输出字段不存在时，MessageBox Step 返回
`RuntimeVariableResolutionError`，不会弹出带有旧值或未替换占位符的误导信息。

| prompt 字段 | 类型 | 必填 | 默认值 | 说明 |
|---|---|---|---|---|
| `mode` | string | no | `confirm` | `confirm`、`notice` 或 `judgment` |
| `title` | string | no | `Operator Action Required` | 弹窗标题 |
| `message` | string | yes | empty | 给操作员看的明确动作说明 |
| `image` | string | no | empty | 可选 PNG/JPG/JPEG 文件名；默认从程序根目录的 `image` 文件夹加载 |
| `confirmText` | string | confirm 模式 | `OK` | 确认按钮文字 |
| `closeOnStep` | string | no | 下一正常 Step | notice 模式关闭目标；该 Step 完成全部 Retry 并进入最终状态后关闭弹窗 |
| `dialogKey` | string | no | empty | 将前面的 notice 与后面的 judgment 绑定到同一个窗口；按 UUT 隔离 |
| `passText` | string | judgment 模式 | `PASS` | 人工通过按钮文字 |
| `failText` | string | judgment 模式 | `FAIL` | 人工失败按钮文字 |
| `failureCode` | string | judgment 模式 | `OperatorCheckFailed` | 点击 FAIL 后写入结果的错误码 |
| `timeoutMs` | number | no | `60000` | confirm/judgment 等待超时；0 表示不限制。notice 用于确认 UI 已成功显示，内部最多等待 5 秒 |

CLI 或其他没有注册交互响应器的运行环境会立即返回
`OperatorPromptResponderUnavailable`，不会无期限卡住。完整示例见
`examples/operator_prompt_sequence.json`。

图片使用规则：

- Flow Editor 的 `Image (optional)` 下拉框自动读取 `PicoATE.UI.exe` 同级 `image` 文件夹。
- 工程开发目录中的 `image` 会在 UI 编译后复制到 Debug/Release 程序目录。
- Sequence 推荐只保存文件名，例如 `fixture_connection.png`，整个工具包换电脑后仍可使用。
- 支持 `.png`、`.jpg`、`.jpeg`，其他扩展名在编译阶段报错。
- 图片缺失或损坏时仍显示文字和确认按钮，同时在弹窗中提示图片不可用；不会把测试项判为 Error。
- 大图片按比例缩小到弹窗可视范围，小图片不会强制拉伸。

Runtime placeholders are allowed inside action `parameters` and `inputs`.
Supported forms include `${var.NAME}`, `${loop.index}`, `${loop.value}`,
`${uut.id}`, `${attempt.index}`, and `${attempt.number}`. Whole-field
placeholders preserve type, while embedded placeholders produce strings:

```json
{
  "id": "measure-channel",
  "kind": "action",
  "moduleId": "mock.measurement",
  "inputs": {
    "outputs": {
      "channel": "${var.channelIndex}",
      "label": "CH${var.channelIndex}",
      "uutId": "${uut.id}"
    }
  }
}
```

See `docs/变量与结果引用.md` for the split between configuration-time and
runtime variables.

## 文本解析的多命名输出

内置 `splitText` 和 `regexCapture` 默认保持单值输出。需要一次拆出多个值时，配置
`resultMode: "multiple"`，并使用 `fields` 声明每个输出的来源、名称和类型：

```json
{
  "id": "parse-response",
  "kind": "action",
  "moduleId": "builtin.data-parser",
  "function": "splitText",
  "inputs": {
    "source": "BTSN001,812.5,0x1A",
    "delimiter": ",",
    "resultMode": "multiple",
    "fields": [
      { "index": 0, "name": "SN1", "type": "string" },
      { "index": 1, "name": "voltage", "type": "number" },
      { "index": 2, "name": "status", "type": "hex" }
    ]
  }
}
```

后续节点分别通过 `${step:parse-response.outputs.fields.SN1}`、
`${step:parse-response.outputs.fields.voltage}` 等表达式读取。Flow Editor 会根据
`fields` 自动把这些名称加入 `fx` 菜单。正则多捕获使用相同结构，但将 `index`
替换为 `group`。完整规则和示例见 `docs/通用数据解析.md` 与
`examples/named_text_fields_sequence.json`。

## 基础数值工具

`builtin.value-tools` 是 Core 内置模块，不依赖业务 DLL。Flow Editor 的
`Basic Functions > Value Tools` 提供以下功能：

| function | UI 名称 | 主要用途 |
|---|---|---|
| `statistics` | Numeric Statistics | 对多路数值计算最小值、最大值、温差、平均值和总和 |
| `calculate` | Basic Calculation | 加减乘除、取模、幂、绝对差、取整、限幅等基础运算 |
| `textToNumber` | Text To Number | 把 `0xC8`、十进制、八进制或二进制文本解析成整数 |
| `numberToText` | Number To Text | 需要文本报文时，把整数格式化为指定进制字符串 |

四路温度统计示例：

```json
{
  "id": "temperature-statistics",
  "kind": "action",
  "moduleId": "builtin.value-tools",
  "function": "statistics",
  "inputs": {
    "values": [
      { "name": "Gun 1", "value": "${step:read-1.outputs.temperature}" },
      { "name": "Gun 2", "value": "${step:read-2.outputs.temperature}" },
      { "name": "Gun 3", "value": "${step:read-3.outputs.temperature}" },
      { "name": "Gun 4", "value": "${step:read-4.outputs.temperature}" }
    ]
  }
}
```

`statistics` 输出 `count`、`sum`、`minimum`、`maximum`、`range`、`average`、
`minimumIndex`、`maximumIndex`、`minimumName` 和 `maximumName`。例如后续 Limit
通过 `${step:temperature-statistics.outputs.range}` 判断四把充电枪的最大温差。

数值和显示格式必须区分：整数 `200` 在内存中已经可以由 Modbus/CAN 插件写成
`0x00C8`，通常不需要先转换；只有插件接口明确要求文本报文时，才使用
`numberToText` 得到 `C8`、`00C8` 或 `0xC8`。完整可运行示例见
`examples/value_tools_sequence.json`。

## Limit 比较节点

`limit` 是引擎内置的纯比较节点，不调用业务 DLL。典型用法是让前面的
RX/解析 Step 把值写入每个 UUT 独立的结果仓库，再通过 Step 表达式把实际值
传给 Limit：

```json
{
  "id": "02",
  "key": "limit",
  "name": "电压判定",
  "kind": "limit",
  "inputs": {
    "actual": "${step:parse.outputs.frame.voltage}"
  },
  "parameters": {
    "comparison": "between",
    "expected": 5.0,
    "tolerance": 0.2,
    "inclusive": true,
    "unit": "V",
    "measurementName": "CAN_VOLTAGE"
  }
}
```

支持的比较方式：

| `comparison` | 必需参数 | 含义 |
|--------------|----------|------|
| `between` / `range` | `lower`+`upper`，或 `expected`+`tolerance` | 区间判断；后一种写法自动计算 `[expected-tolerance, expected+tolerance]`，`inclusive` 默认为 `true` |
| `>` / `>=` / `<` / `<=` | `expected`，也可分别使用 `lower`/`upper` | 数值单边判断 |
| `equal` / `==` / `notEqual` / `!=` | `expected`；数值可加 `tolerance` | 数值容差或字符串相等判断 |
| `contains` / `startsWith` / `endsWith` | `expected` | 字符串判断，区分大小写 |
| `isTrue` / `isFalse` | 无 | 严格布尔判断，不接受字符串 `"true"` |

### 判定项与上下限显示规则

- 是否属于判定项，只看节点有没有明确的 Limit 比较语义，不按插件类别猜测。当前标准
  判定节点是 `kind: "limit"`。
- CAN、Modbus、VISA、Serial 等读取或解析 Step 负责产生值，不会因为“输出了数值”就
  自动判定 Pass/Fail；需要判定时，在后面增加 Limit，并通过表达式引用解析结果。
- Sequence 编译成功后，ExecutionReport 会立即为所有 Limit 生成只读判定规格。因此
  Run Test 页面在开始运行前就显示 Lower/Upper，不再等节点执行到当前位置才出现。
- `between`、单边比较和数值容差会显示计算后的数值上下限；字符串、布尔值等没有数值
  区间的比较，会在 Lower/Upper 位置显示比较基准，避免操作员看不到期望值。
- 运行前 Actual 显示 `-`。节点执行后只更新 Actual、Result、ErrorCode 和用时，并保留
  同一份判定规格；节点被跳过或实际值解析失败时，也不会丢失已经配置的上下限。
- `expected` 或上下限是运行时变量表达式时，运行前先显示表达式；运行后报告使用解析后
  的有效值。这样工程师在开跑前仍能确认规则来源。

比较不通过时，节点结果是 `Failed`，错误码为 `LimitFailed`。运行时优先由 Station
的 `stopOnFailure` 决定停止还是继续；未加载 Station 配置时才使用节点的
`errorPolicy.onFail`。实际值缺失、类型错误、上下限配置错误
属于配置或数据问题，节点结果是 `Error`，不会伪装成产品测试失败。

Limit 总会生成一条 `MeasurementResult`，包含实际值、单位、上下限或比较基准、
判定状态和错误信息；CLI、ExecutionReport 和 UI 使用同一份结构化结果。

Limit 执行时同时发布实时诊断日志：`LIMIT_CHECK` 记录取到的实际值、比较方式和
配置阈值，`LIMIT_RESULT` 记录最终 PASS/FAIL 以及生效的上下限。日志用于定位问题，
最终报告仍以结构化 `MeasurementResult` 为准。

同时提供 `lower/upper` 和 `expected/tolerance` 时，以显式上下限为准；只提供
`lower` 或只提供 `upper` 会返回配置错误，避免引擎猜测用户意图。

完整可运行示例：`examples/scoped_result_sequence.json`。

## CAN 参数与测量显示约定

CAN 插件的 `PicoATE_Describe` 必须为发送 ID、过滤 ID 和过滤 Mask 提供明确范围提示。
Flow Editor 在保存或编译前校验字面量；表达式要等运行时解析后由插件再次校验。

| 字段 | 有效范围/语义 |
|------|---------------|
| 标准帧 `id` | `0x000~0x7FF`；`extended=false` 时超过范围直接报错 |
| 扩展帧 `id` | `0x00000000~0x1FFFFFFF`；必须启用 `extended` |
| `filterId` | `0x00000000~0x1FFFFFFF` |
| `filterMask=0x00000000` | 不按 CAN ID 过滤，接受任意 ID |
| `filterMask=0x000007FF` | 标准帧 11 位精确匹配 |
| `filterMask=0x1FFFFFFF` | 扩展帧 29 位精确匹配；不是“关闭过滤” |

Read CAN Frame 返回一条结构化测量。Lower 和 Upper 都显示完整过滤条件，例如
`ID=0x199 | MASK=0x7FF`；Actual 显示实际帧，例如
`ID=0x199 | MASK=0x7FF | DATA=09 01 03 07 | DLC=4`。GCAN、CX 及后续复用
`CanPluginBridge` 的 CAN 插件共享此规则，UI 不写死任何厂商名称。

## Test Item

A `testItem` is a scheduler-owned composite result boundary. It does not call a
business module directly. Its child steps run in order, and the parent result is
calculated after every child reaches a terminal state:

| Child results | Parent result |
|---------------|---------------|
| every child is `Passed` | `Passed` |
| any child is `Failed` or otherwise not passed | `Failed` |
| any child is `Timeout` and none is `Error` | `Timeout` |
| any child is `Error` | `Error` |

TestItem 是否局部 fail-fast 由 Station 的 `stopOnFailure` 统一决定：

- `true`：子 Step 在 Retry 结束后仍为 `Failed/Error/Timeout`，后续兄弟步骤及其
  嵌套子树标记为 `Skipped`；父项汇总失败后停止外层主流程并进入 Cleanup。
- `false`：失败子 Step 保留真实结果，但继续执行同一 TestItem 的后续步骤；父项按
  `Error > Timeout > Failed > Passed` 汇总，外层后续测试项继续执行。

Cleanup 分组允许使用 TestItem 收纳多个关闭动作。父 TestItem 和每个启用的 Cleanup
子步骤都必须真实执行并进入报告，不能只显示父项 Passed。Cleanup TestItem 继续遵守
子步骤顺序，并使用 `alwaysRun` 的 Cleanup 激活语义。

“失败继续”不代表忽略数据依赖。后续步骤引用失败节点输出时会得到明确的运行时
变量解析 `Error`，不会永久停在 Waiting 状态；不依赖该输出的后续步骤仍继续。
没有加载 Station 配置时，保留旧的逐节点 `errorPolicy` 行为以兼容历史脚本。

TestItem 和普通 Step 都可以配置 Retry，但作用范围不同：

- 子 Step 的 `retry.maxAttempts` 只重试该子 Step；
- 父 TestItem 的 `retry.maxAttempts` 重试整个 TestItem 子树，从第一个启用的子步骤重新开始；
- 父 TestItem 每开始新一轮，子 Step 自己的 Retry 次数重新计算；
- 上一轮的父项和子步骤 Attempt 历史保留在报告中，不会被新一轮覆盖；
- TestItem 重试前恢复本 UUT 在该轮开始前的变量快照，并重置内部 Loop 状态；
- 设备连接、已经发出的总线报文和外部仪器状态不能事务回滚。需要恢复硬件状态时，
  应在 TestItem 子步骤或 Cleanup 中显式执行；
- TestItem 重试时，其子树发出的活动 MessageBox 会关闭，新一轮按流程重新创建；
- 配置整体 Retry 的 TestItem 内不能包含 Barrier。Barrier 涉及多个 UUT 的同步成员，
  当前版本不会猜测其他 UUT 是否也应一起回退，编译器会直接报错。

```json
{
  "id": "power-rail-check",
  "name": "Power Rail Check",
  "kind": "testItem",
  "steps": [
    { "id": "measure-5v", "kind": "action" },
    { "id": "measure-3v3", "kind": "action" }
  ]
}
```

Current constraints:

- `testItem` 支持任意层嵌套，也可以直接包含 `loop`；
- 嵌套 `loop` 仍未支持；
- 父 TestItem 和具体子步骤均支持 Retry，二者的次数预算彼此独立；
- 配置整体 Retry 的 TestItem 内不允许包含 Barrier；
- disabled children are not compiled and do not participate in aggregation;
- `ExecutionReport` and Runner UI retain the parent-child hierarchy.

Runnable example: `examples/test_item_sequence.json`.

## Loop Object

Loop steps are scheduler-owned control nodes. They do not call business
modules directly. The scheduler keeps the `ExecutionPlan` immutable, stores the
per-UUT loop cursor in `LoopController`, sets the loop variable on the UUT
runtime variables map, and releases the child body steps for each iteration.

当前支持固定次数 `for` 和条件采样 `condition` 两类 Loop。

### For Loop

| Field | Type | Required | Default | Notes |
|-------|------|----------|---------|-------|
| `type` | string | no | `for` | 固定次数循环使用 `for`。 |
| `variable` | string | no | `i` | Name written into `UutExecution::variables`. |
| `from` | number | no | `0` | Inclusive start value. |
| `to` | number | no | `0` | Inclusive end value. |
| `step` | number | no | `1` | Must not be zero; can be negative. |

Example:

```json
{
  "id": "repeat-measurements",
  "name": "Repeat Measurements",
  "kind": "loop",
  "loop": {
    "type": "for",
    "variable": "sampleIndex",
    "from": 0,
    "to": 2,
    "step": 1
  },
  "steps": [
    {
      "id": "measure-sample",
      "kind": "action",
      "moduleId": "mock.measurement",
      "function": "measureVoltage",
      "inputs": {
        "outputs": {
          "sampleIndex": "${var.sampleIndex}",
          "sampleLabel": "sample-${var.sampleIndex}"
        },
        "measurements": {
          "name": "LOOP_SAMPLE_${var.sampleIndex}",
          "value": "${loop.value}",
          "unit": "V"
        }
      }
    }
  ]
}
```

Runtime behavior:

| Case | Behavior |
|------|----------|
| positive step and `from <= to` | Runs values `from, from + step, ... to`. |
| negative step and `from >= to` | Runs values `from, from + step, ... to`. |
| range produces zero values | Loop body is marked `Skipped`; execution continues after the loop. |
| nested loops | Rejected by `PlanBuilder` for now; add a separate sequence or unroll manually. |

Loop body node executions are recorded as repeated attempts on the same body
node activation. `ExecutionReport` exposes the loop body relationship on
`StepReport::loop`, and each `AttemptReport::loopIteration` carries the
iteration number, zero-based index, variable name, and value so UI/report
consumers do not have to infer iteration data from attempt order.

### While Loop、Break If、Counter 与 Aggregate

While Loop 只负责一件事：重复执行循环体，直到 `Break If` 命中，或者保护上限触发。
它不再内置“连续通过次数”和“统计采样值”等特定业务语义。计数由 `Counter` 完成，
最小值、最大值和平均值由 `Aggregate` 完成，退出判断由 `Break If` 完成。

#### While Loop 字段

| Field | Type | Required | Default | Notes |
|-------|------|----------|---------|-------|
| `type` | string | yes | - | 固定写 `while`。 |
| `intervalMs` | number | no | `0` | 两轮之间的等待时间。 |
| `maxIterations` | number | no | `100` | 最大循环次数；`0` 表示关闭。 |
| `timeoutMs` | number | no | `60000` | 循环整体超时；`0` 表示关闭。 |
| `iterationErrorPolicy` | string | no | `abortLoop` | `abortLoop` 或 `continueLoop`。 |

至少配置一个非零的 `maxIterations` 或 `timeoutMs`，并且循环体中必须存在一个启用的
`Break If`。这是编译期规则，用于避免无退出路径的死循环。

#### Break If

`Break If` 使用与 Limit 相同的结构化比较规则，例如 `equal`、`notEqual`、
`greaterThan`、`greaterOrEqual`、`lessThan`、`lessOrEqual` 和 `between`。
未命中时节点仍然是 `Passed`，循环继续；命中后退出最近一层 Loop，并把本轮尚未执行的
后续节点标记为 `Skipped`。For Loop 同样支持 `Break If`。

#### Counter

| Field | Location | Notes |
|-------|----------|-------|
| `condition` | `inputs` | 可选。留空表示每次执行都计数；也可填写布尔值或布尔表达式。 |
| `mode` | `parameters` | `consecutive` 连续计数；`total` 累计计数。 |
| `start` | `parameters` | 初始值，默认 `0`。 |
| `increment` | `parameters` | 每次命中增加的值，默认 `1`。 |

Counter 在每次进入新的 Loop 时自动清零。输出包含 `value`、`condition` 和 `mode`。
`consecutive` 模式在 condition 为 false 时恢复为 `start`；`total` 模式保留已有计数。

#### Aggregate

Aggregate 从 `inputs.value` 接收数值，每轮累积一次，在新的 Loop 开始时自动清零。
输出包含 `last`、`count`、`sum`、`minimum`、`maximum` 和 `average`。这些输出会出现在
属性编辑器的 `fx` 选择菜单中，可直接供后续 Limit、Counter 或 Break If 引用。

```json
{
  "id": "wait-voltage-stable",
  "name": "Wait Voltage Stable",
  "kind": "loop",
  "loop": {
    "type": "while",
    "intervalMs": 200,
    "maxIterations": 500,
    "timeoutMs": 60000,
    "iterationErrorPolicy": "continueLoop"
  },
  "steps": [
    { "id": "read-voltage", "kind": "action" },
    {
      "id": "voltage-stats",
      "kind": "aggregate",
      "inputs": { "value": "${step:read-voltage.outputs.voltage}" }
    },
    {
      "id": "voltage-ok",
      "kind": "limit",
      "inputs": { "actual": "${step:read-voltage.outputs.voltage}" },
      "limit": { "comparison": "greaterOrEqual", "expected": 800 }
    },
    {
      "id": "stable-count",
      "kind": "counter",
      "inputs": { "condition": "${step:voltage-ok.outcome}" },
      "parameters": { "mode": "consecutive", "start": 0, "increment": 1 }
    },
    {
      "id": "stable-enough",
      "kind": "break",
      "inputs": { "actual": "${step:stable-count.outputs.value}" },
      "limit": { "comparison": "greaterOrEqual", "expected": 30 }
    }
  ]
}
```

运行语义：

- 循环体自己的 Retry 先执行，Retry 耗尽后的最终结果再交给 While Loop；
- `iterationErrorPolicy: abortLoop` 遇到失败立即结束，`continueLoop` 允许下一轮重新采样；
- `Cancelled` 始终退出，Stop/Abort 仍由会话安全收口；
- 每轮执行记录为循环体节点的新 Attempt，报告保留完整采样历史；
- While Loop 可以放入 TestItem，只有循环最终失败才交给 TestItem 汇总；
- While Loop 最终输出 `iterations`、`elapsedMs`、`exitReason` 和 `breakNodeId`；
- While Loop 内暂不允许 Barrier，嵌套 Loop 仍不支持；
- 旧的 `type: condition` 不再支持，编译器会提示迁移到 While + Break If。

完整示例：`examples/while_loop_sequence.json`。

## Resource Requirement

| Field | Type | Required | Default | Notes |
|-------|------|----------|---------|-------|
| `resourceId` / `name` | string | yes | empty | `resourceId` takes precedence over `name`. |
| `mode` | string | no | `exclusive` | See modes below. |
| `count` | number | no | `1` | For counted resources. |
| `priority` | number | no | `0` | Higher priority can be used by resource policies. |
| `acquireTimeoutMs` | number | no | `30000` | Acquire timeout in milliseconds. |

Resource modes:

```text
exclusive
sharedRead
sharedWrite
counted
orderedExclusive
```

## Retry Object

| Field | Type | Required | Default |
|-------|------|----------|---------|
| `maxAttempts` | number | no | `1` |
| `delayMs` | number | no | `0` |
| `retryWhen` | string | no | empty |

`retry` 放在普通 Step 上时只重新调用该 Step；放在 TestItem 上时重新执行整个
TestItem 子树。`maxAttempts` 包含第一次执行，例如 `2` 表示首次失败后最多再执行一次。

## Timeout Object

| Field | Type | Required | Default |
|-------|------|----------|---------|
| `timeoutMs` | number | no | `0` |

## Error Policy Object

| Field | Type | Required | Default |
|-------|------|----------|---------|
| `onFail` | string | no | `StopUut` |
| `onError` | string | no | `StopUut` |
| `onTimeout` | string | no | `StopUut` |
| `cleanupRegionId` | string | no | empty |
| `stopUutOnFailure` | bool | no | `true` |

Error actions:

```text
Continue
StopUut
Retry
RunCleanup
Abort
```

当前 UI 的普通属性页不再要求工程师逐 Step 配置这三个动作。字段仍作为旧脚本兼容和
Advanced JSON 能力保留。运行优先级为：

1. 当前 Step 或 TestItem 的 Retry；
2. Station `stopOnFailure` 统一停止/继续策略；
3. 未加载 Station 配置时，使用本对象的 `errorPolicy`。

用户 Stop、Abort、Cancelled 不会被 Station 的“失败继续”转换成 Continue。

## Barrier Object

| Field | Type | Required | Default |
|-------|------|----------|---------|
| `barrierName` | string | no | step id |
| `cohortId` | string | no | `default` |
| `expectedUutCount` | number | no | `-1` |
| `quorumCount` | number | no | `-1` |
| `quorumRatio` | number | no | `1.0` |
| `arrivalTimeoutMs` | number | no | `60000` |
| `releaseTimeoutMs` | number | no | `5000` |
| `arrivalPolicy` | string | no | `WaitAll` |
| `releasePolicy` | string | no | `Lockstep` |
| `failurePolicy` | string | no | `FailBarrier` |
| `timeoutPolicy` | string | no | `FailArrivedAndWaiting` |
| `releaseHeldResourcesOnWait` | bool | no | `true` |

Arrival policies:

```text
WaitAll
DropFailed
CountFailed
Quorum
BestEffort
ManualDecision
```

Release policies:

```text
Lockstep
Latch
Cohort
RollingWindow
```

Failure policies:

```text
FailBarrier
RemoveFailedMember
HoldFailedMember
ContinueWithWarning
AbortCohort
```

Timeout policies:

```text
FailArrivedAndWaiting
ReleaseArrived
ReleaseIfQuorumReached
AbortCohort
RequestOperatorDecision
```

## Unknown Fields

Unknown fields are reported as compile warnings and do not block compilation.
This helps catch misspelled JSON fields without preventing station-specific
metadata from evolving.

| Case | Current behavior |
|------|------------------|
| unknown field | warning |
| fields prefixed with `x-` | allowed extension |
| `vendor` object | allowed extension namespace |

`parameters`, `metadata`, `vendor`, and `x-*` fields are open extension areas.
The compiler does not inspect nested fields inside those objects.

This keeps production sequence files clean while still leaving room for
station-specific or plugin-owned metadata.

Potential future strict mode:

| Case | Possible behavior |
|------|-------------------|
| unknown field in strict mode | error |
| unknown field in default mode | warning |

## 作用域 ID、Key 与结果引用

顶层 TestItem 和独立 Step 推荐使用字符串编号：

```json
{ "id": "001", "name": "CAN请求", "kind": "testItem" }
```

子步骤可以添加当前父节点内唯一的语义 Key：

```json
{ "id": "03", "key": "rx", "name": "接收响应", "kind": "action" }
```

编译后的节点路径为 `001.rx`。不同 TestItem 可以重复使用 `tx`、`rx`、`parse`。

```json
"frame": "${step:001.rx.outputs.frame}"
```

当前 TestItem 内可使用相对 Key：

```json
"actual": "${step:parse.outputs.voltage}"
```

引用会在编译期检查节点存在性和执行先后关系，并生成数据依赖边。完整规则见
[变量与结果引用](变量与结果引用.md)，可运行示例见
`examples/scoped_result_sequence.json`。

# 资源区间（跨 Step 持锁）

当多台 UUT 共用同一台仪器，并且一段连续操作必须作为完整事务执行时，可以在首尾 Step 上设置资源区间：

```json
{
  "id": "006",
  "name": "CAN Send",
  "kind": "action",
  "resourceRegionStart": {
    "id": "resource-region-001",
    "resources": [
      {
        "resourceId": "CAN1.CH1",
        "mode": "exclusive"
      }
    ]
  }
},
{
  "id": "010",
  "name": "CAN Check",
  "kind": "limit",
  "resourceRegionEnd": "resource-region-001"
}
```

运行时会在 `006` 开始前获取 `CAN1.CH1`，执行到 `010` 结束后再释放。其他 UUT 在进入点等待，因此可以得到 `UUT-1 Send -> Read -> Check` 完成后再运行 `UUT-2 Send -> Read -> Check` 的顺序。区间内 Step 原有的同名节点资源会被视为已由区间持有，不会重复加锁。

如果要让一个 TestItem 的全部子步骤和整体 Retry 共用同一把锁，应将起止标记同时放在 TestItem 本身：

```json
{
  "id": "001",
  "name": "CAN Transaction",
  "kind": "testItem",
  "retry": { "maxAttempts": 3 },
  "resourceRegionStart": {
    "id": "resource-region-001",
    "resources": [
      {
        "resourceId": "CAN1.CH1",
        "mode": "exclusive"
      }
    ]
  },
  "resourceRegionEnd": "resource-region-001",
  "steps": []
}
```

这种单项 TestItem 锁只申请一次。TestItem 整体 Retry 时继续持有原租约，直到最终通过、重试耗尽、停止或异常收口后才释放，其他 UUT 在此期间不能进入。若把 `LOCK` 和 `UNLOCK` 分别放在 TestItem 内部的两个子 Step 上，则锁只覆盖每轮执行的该子区间，TestItem 整体 Retry 之间允许其他 UUT 使用资源。

当前约束：`LOCK` 和 `UNLOCK` 必须位于同一父级；普通区间的 `UNLOCK` 必须晚于 `LOCK`；单项锁允许起止点为同一 Step 或 TestItem。区间不可交叉、嵌套、缺少一端，也不可包含 Barrier。失败、Stop 或跳过 `UNLOCK` 时，调度器仍会释放租约。复制 Step 时不会复制资源区间标记，避免生成重复区间 ID。

# 周期后台 Action（MVP）

普通插件 Action 可以在 Setup 顶层增加 `periodic`，把该 Action 注册为覆盖本次测试主流程的周期任务：

```json
{
  "id": "heartbeat",
  "name": "Send Heartbeat",
  "kind": "action",
  "moduleId": "device",
  "function": "write",
  "inputs": {
    "deviceId": "MODBUS1",
    "address": 1,
    "value": "${periodic.counter}"
  },
  "periodic": {
    "intervalMs": 5000,
    "runImmediately": true,
    "counter": {
      "start": 1,
      "increment": 1,
      "wrapAt": 255
    }
  }
}
```

| 字段 | 必填 | 含义 |
|---|---|---|
| `intervalMs` | 否 | 两次执行之间的间隔，默认 5000 ms，必须大于 0 |
| `runImmediately` | 否 | 注册后是否立即执行一次，默认 `true` |
| `counter.start` | 否 | 第一次实际执行使用的计数值，默认 `1` |
| `counter.increment` | 否 | 每次实际执行后的递增量，默认 `1`，必须大于 0 |
| `counter.wrapAt` | 否 | 包含式回绕上限；`0` 表示不回绕，默认 `0` |

周期 Action 的输入可以使用：

| 表达式 | 含义 |
|---|---|
| `${periodic.counter}` | 按 `start/increment/wrapAt` 生成的业务计数值 |
| `${periodic.index}` | 本任务实际执行序号，从 `0` 开始 |
| `${periodic.number}` | 本任务实际执行次数，从 `1` 开始 |
| `${periodic.requestId}` | 本次调用唯一 requestId |

资源暂时被事务锁占用时，本次触发只会延后，不会增加计数；插件实际返回后才进入
下一计数值。Fail、Error、Timeout 也属于一次已经完成的调用，因此下一周期继续递增。
若 `start=1`、`increment=1`、`wrapAt=255`，计数顺序为
`1, 2, ... 255, 1, ...`。

当前约束：

1. 只支持 Setup 组顶层的单个 `action`，不支持放进 TestItem、Loop 或 Cleanup。
2. 周期 Action 不使用 Step Retry；一次失败后等待下一周期再次执行，主流程继续，最终 Session 记为失败。
3. 必须声明资源；若 `inputs.deviceId` 是固定值，编译器会自动取通道前的设备 ID，例如 `CAN1.CH2` 自动使用独占资源 `CAN1`。动态 `deviceId` 必须显式填写 `resources`。
4. 任务使用与主流程相同的 `ResourceManager`。事务区间持有同一资源时，本次周期执行会延后，不会插入 Send/Read 等事务中间。
   设备级资源与通道资源按层级互斥，例如 `CAN1` 与 `CAN1.CH1` 视为同一资源树。
5. 每次触发生成独立 `requestId`，不允许同一任务重入，也不会积压补跑错过的周期。
6. 所有 UUT Main 结束或 Session Stop 后自动取消定时请求，再进入 Cleanup，不需要额外 Stop Step。
7. 当前为调度线程协作式执行。普通插件调用若长时间阻塞，周期任务会延后；本版本不会另起线程强行并发进入厂家 DLL。

完整运行边界见 [周期后台任务](周期后台任务.md)。
