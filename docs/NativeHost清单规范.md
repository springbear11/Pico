# PicoATE NativeHost Manifest

`PicoATE.NativeHost.exe` can load DLL settings from a manifest JSON file. The
manifest describes only the DLL host boundary; sequence flow, resources, retry,
cleanup, and test items remain in the sequence JSON.

## Command Line

Preferred manifest mode:

```powershell
PicoATE.NativeHost.exe `
  --manifest examples\nativehost\test_dll_manifest.json `
  --project-dir D:\Work\PicoATE `
  --var PICOATE_TEST_DLL=D:\path\to\PicoATE.TestDllModule.dll
```

Legacy mode remains supported:

```powershell
PicoATE.NativeHost.exe --dll D:\path\to\VendorDriver.dll
```

## Manifest Object

```json
{
  "dll": "${PICOATE_TEST_DLL}",
  "symbol": "PicoATE_Execute",
  "bufferSize": 65536,
  "dllTimeoutMs": 30000,
  "metadata": {
    "name": "PicoATE Test DLL"
  }
}
```

| Field | Type | Required | Default | Notes |
|-------|------|----------|---------|-------|
| `dll` / `dllPath` | string | yes | empty | DLL path. Relative paths resolve from the manifest directory. |
| `symbol` | string | no | `PicoATE_Execute` | Exported C ABI function. |
| `bufferSize` | number | no | `65536` | Response buffer size in bytes. Must be positive. |
| `dllTimeoutMs` | number | no | `30000` | In-host DLL call timeout. Parent `QProcessTransport` timeout still applies. |
| `metadata` | object | no | `{}` | Optional diagnostic/project metadata. |

## 诊断与日志批次配置

manifest 可选增加 `diagnostics`：

```json
"diagnostics": {
  "vendorStdio": "strict",
  "maximumBufferedLogs": 1024,
  "maximumMessageCharacters": 4096,
  "maximumBatchRecords": 64,
  "maximumBatchBytes": 16384,
  "batchFlushMs": 20
}
```

| 字段 | 默认值 | 说明 |
|------|--------|------|
| `vendorStdio` | `strict` | `strict` 捕获并转发厂家 stdout/stderr，Response 前等待 flush；`discard` 只排空并丢弃，不做每 Step flush。 |
| `maximumBufferedLogs` | `1024` | Host 内最多缓存的日志记录数，超出后丢弃并生成摘要。 |
| `maximumMessageCharacters` | `4096` | 单条日志保留的最大字符数。 |
| `maximumBatchRecords` | `64` | 一个 `moduleLogBatch` 最多包含的日志条数。 |
| `maximumBatchBytes` | `16384` | 一个日志批次的近似字节上限。 |
| `batchFlushMs` | `20` | 低频日志等待成批的最长时间，Response 会立即冲刷剩余日志。 |

自有插件已经使用 `PicoATE_Log` 时可选择 `discard`，减少每个短 Step 的标准流顺序屏障。
不受控厂家 DLL 或仍需查看其 `printf/fprintf` 输出时使用默认 `strict`。

旧版单条 `moduleLog` 仍可解析；新版 Host 使用 `moduleLogBatch` 降低 JSON 序列化和
管道唤醒次数。1000 条测试日志在默认 64 条批次下产生 16 个日志帧和 1 个 Response。

## Variable Resolution

Manifest string fields use `VariableResolver`, documented in
`docs/变量与结果引用.md`.

Available sources:

- `--var NAME=VALUE` command-line assignments
- built-ins such as `${SEQUENCE_DIR}` and `${PROJECT_DIR}`
- environment variables

When NativeHost is launched through sequence `moduleBindings`, arguments are
resolved once by the scheduler and then passed to NativeHost:

```json
{
  "program": "${PICOATE_NATIVE_HOST}",
  "arguments": [
    "--manifest",
    "${PROJECT_DIR}/examples/nativehost/test_dll_manifest.json",
    "--project-dir",
    "${PROJECT_DIR}",
    "--var",
    "PICOATE_TEST_DLL=${PICOATE_TEST_DLL}"
  ]
}
```

This two-step resolution is intentional:

```text
Sequence JSON variables
  -> ModuleBindingRegistrar
  -> NativeHost command-line arguments
  -> NativeHost manifest variables
  -> DLL load configuration
```

It keeps station/project values outside the manifest while letting the manifest
remain reusable across machines and build directories.

## Boundary Rule

Do not put scheduler semantics into the manifest. These belong in sequence
JSON:

- step order
- resource requirements
- retry/timeout/error policy
- cleanup and barrier behavior
- test inputs and limits

The manifest is only for the DLL host loading contract.

## Included Examples

| Manifest | Sequence | Purpose |
|----------|----------|---------|
| `examples/nativehost/test_dll_manifest.json` | `examples/nativehost_dll_sequence.json` | Minimal DLL echo test for NativeHost plumbing |
| `examples/nativehost/can_decode_manifest.json` | `examples/can_dll_sequence.json` | Pure software CAN decode and limit-check example; no CAN analyzer required |

The CAN example intentionally validates the integration boundary without
hardware. Real CAN hardware, vendor SDKs, DBC parsing, or station-specific
message acquisition should live inside the project DLL or another external
module behind the same JSON contract.

