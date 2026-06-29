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
| `moduleBindings` | array | no | `[]` | Runtime module transport bindings. |
| `groups` | array | yes | none | Array of group objects. |

## Module Binding Object

`moduleBindings` connects action step `moduleId` values to runtime module
implementations. These bindings are not compiled into `ExecutionPlan`; they are
used by runtime setup to register modules on `ExecutionSession`.

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
`docs/variable_resolver.md`.

NativeHost DLL module bindings should prefer `--manifest` arguments for DLL
load settings. The manifest format is documented in
`docs/nativehost_manifest.md`.

## Group Object

| Field | Type | Required | Default | Notes |
|-------|------|----------|---------|-------|
| `id` | string | no | JSON path | Stable group id. |
| `name` | string | no | `id` | Display name. |
| `kind` / `type` | string | no | `custom` | `setup`, `main`, `cleanup`, or `custom`. |
| `enabled` | bool | no | `true` | Disabled groups are not compiled into the plan. |
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
| `resultRecording` | bool | no | `true` | Copied to `ExecNode::resultRecording`. |
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
| `steps` | array | yes for loop steps | none | Child steps that form the loop body. |
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
| `statement` | currently mapped to `Action` |
| `sequenceCall` | currently mapped to `Action` |

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

See `docs/variable_resolver.md` for the split between configuration-time and
runtime variables.

## Loop Object

Loop steps are scheduler-owned control nodes. They do not call business
modules directly. The scheduler keeps the `ExecutionPlan` immutable, stores the
per-UUT loop cursor in `LoopController`, sets the loop variable on the UUT
runtime variables map, and releases the child body steps for each iteration.

The first implementation supports fixed `for` loops only:

| Field | Type | Required | Default | Notes |
|-------|------|----------|---------|-------|
| `type` | string | no | `for` | Currently only `for` is supported. |
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
