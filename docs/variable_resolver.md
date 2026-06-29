# PicoATE Variable Resolver

PicoATE has two variable replacement phases:

| Phase | Resolver | Examples | Timing |
|-------|----------|----------|--------|
| Configuration-time | `VariableResolver` | `${PROJECT_DIR}`, `${SEQUENCE_DIR}`, `${PICOATE_NATIVE_HOST}` | Before module registration / manifest loading |
| Runtime | `RuntimeVariableResolver` | `${var.sampleIndex}`, `${loop.index}`, `${uut.id}`, `${attempt.number}` | Immediately before a node handler executes |

`VariableResolver` centralizes `${NAME}` replacement for PicoATE configuration.
It is intentionally independent from `ModuleBindingRegistrar` so future
configuration surfaces, such as NativeHost manifests and station settings, can
reuse the same rules.

## Inputs

```cpp
struct VariableResolverOptions {
    QString sequenceFilePath;
    QString projectDir;
    QHash<QString, QString> variables;
    bool useEnvironment = true;
};
```

Resolution order:

| Source | Example | Notes |
|--------|---------|-------|
| Explicit variables | `${PICOATE_NATIVE_HOST}` | `variables` wins over all other sources. |
| Built-in variables | `${SEQUENCE_DIR}`, `${PROJECT_DIR}` | Derived from `sequenceFilePath` and `projectDir`. |
| Environment variables | `${PYTHON_EXE}` | Enabled by default through `useEnvironment`. |

Empty explicit values are allowed. Missing variables produce structured
`VariableResolutionError` entries instead of silently replacing with an empty
string.

## Recursive Replacement

Variables may reference other variables:

```cpp
options.variables.insert("MODULE_DIR", "${PROJECT_DIR}/modules");
options.variables.insert("CAN_DLL", "${MODULE_DIR}/ProjectCan.dll");
```

Resolving `${CAN_DLL}` expands through both variables. A depth guard prevents
accidental infinite recursion.

## Container Replacement

The resolver supports:

- `QString`
- `QVariantMap`
- `QVariantList`
- `QStringList`

Nested maps and lists are processed recursively. Non-string scalar values such
as numbers and bools are preserved unchanged.

Example:

```json
{
  "program": "${PICOATE_NATIVE_HOST}",
  "arguments": [
    "--dll",
    "${PROJECT_DIR}/modules/ProjectCan.dll",
    {
      "station": "${STATION_ID}"
    }
  ],
  "timeoutMs": 3000
}
```

## Error Paths

Every unresolved variable includes the logical configuration path:

```text
moduleBindings[0].arguments[1]: Unresolved variable: PICOATE_TEST_DLL
```

This is important for UI diagnostics and future strict project validation.

## Current Use

`ModuleBindingRegistrar` now uses `VariableResolver` for `program` and
`arguments`. Plain command names are still left for `QProcess` to resolve
through PATH, while relative paths containing a path separator are resolved
relative to the sequence file directory.

## Future Use

The next configuration features should reuse this resolver rather than adding
new string replacement logic:

- NativeHost manifest paths and arguments.
- NativeHost manifest DLL path, symbol, and metadata.
- Station/project settings.
- External module configuration blocks.
- Future report/export paths.

## Runtime Variables

`RuntimeVariableResolver` is separate from `VariableResolver` because runtime
values can be typed. For example:

```json
{
  "channel": "${var.channelIndex}",
  "label": "CH${var.channelIndex}"
}
```

If the whole field is one variable expression, the original type is preserved:

| JSON value | Runtime value |
|------------|---------------|
| `"${var.channelIndex}"` where `channelIndex` is int `3` | int `3` |
| `"CH${var.channelIndex}"` | string `"CH3"` |
| `"${var.enabled}"` where `enabled` is bool `true` | bool `true` |

Runtime variables are resolved from `NodeExecutionContext` and the current
`UutExecution::variables` map. Supported built-ins:

| Variable | Meaning |
|----------|---------|
| `${uut.id}` | Current UUT id. |
| `${frame.id}` | Current execution frame id. |
| `${attempt.id}` | Current attempt id. |
| `${attempt.index}` | Zero-based attempt index for the current node activation. |
| `${attempt.number}` | One-based attempt number for display/config convenience. |
| `${var.NAME}` | Value from `UutExecution::variables["NAME"]`; nested maps can use dotted paths. |
| `${loop.index}` | Zero-based loop iteration index for the current UUT. |
| `${loop.number}` | One-based loop iteration number. |
| `${loop.value}` | Current loop variable value. |
| `${loop.variable}` | Current loop variable name. |

Bare `${NAME}` also resolves from `UutExecution::variables["NAME"]` for concise
project JSON, but `${var.NAME}` is preferred when the value is a product or
loop variable.

Runtime replacement happens on a temporary copy of `ExecNode::payload` inside
`NodeRunner`. The immutable `ExecutionPlan` is not modified, and business
modules receive ordinary resolved inputs.
