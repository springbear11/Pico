# PicoATE Project Vision

## North Star

PicoATE is intended to become a configurable ATE test platform where project
delivery does not require changing the UI layer or the scheduler framework.

For a new product or station project, the preferred delivery model is:

```text
Add or replace business test modules
+ provide protocol/message parsers when needed
+ describe the test flow and test items in JSON/configuration
= deliver a new test project without modifying UI or scheduler code
```

Examples of project-specific work that should live below the scheduler:

- CAN/LIN/serial protocol message parsing.
- Instrument command wrappers.
- Product-specific test algorithms.
- Measurement conversion and limit evaluation.
- Fixture or station-specific hardware actions.

PicoATE only provides built-in loading support for C/C++ DLL modules and Python
scripts. Other languages should be packaged by the project team as standalone
`.exe` programs that follow the external module JSON contract; the framework
does not provide SDKs or templates for those languages.

Examples of work that should remain framework-owned and reusable:

- UI layout, execution monitoring, editing tools, and reporting views.
- JSON compilation, execution plan building, and scheduling.
- Retry, timeout, cleanup, barrier, resource, and error policy semantics.
- Runtime reports, diagnostics, snapshots, and future session recovery.

## Three-Layer Architecture

```text
UI Layer
  -> edits configuration, starts/stops execution, displays reports
  -> consumes ViewModel / ExecutionReport DTOs
  -> must not contain product test logic

Scheduler Layer
  -> JSON -> SequenceDef -> ExecutionPlan -> ExecutionSession
  -> owns retry, cleanup, barrier, resource, timeout, and error policy
  -> calls business logic only through IModule / IModuleTransport
  -> must not contain CAN/DLL/instrument/product-specific logic

Business Test Logic Layer
  -> implements IModule or external module hosts behind IModuleTransport
  -> owns protocol parsing, instrument control, measurements, and project logic
  -> returns ModuleResult / ModuleTransportResponse to the scheduler
```

The key dependency rule is one-way:

```text
UI depends on scheduler-facing DTOs
Scheduler depends on stable module interfaces
Business modules depend on project libraries and devices
Business modules do not force UI or scheduler changes
```

## Configuration-First Delivery

Test sequences should be described by JSON or a future schema-compatible
configuration format. The configuration owns:

- Step order and grouping.
- Module id and function name.
- Module inputs.
- Resource requirements.
- Retry and timeout policy.
- Error policy and cleanup behavior.
- Barrier and batch coordination.
- Fixed for-loop control flow.
- Checkpoint and reporting flags.

A typical configurable action should look like this:

```json
{
  "id": "read-vin",
  "kind": "action",
  "moduleId": "project.can",
  "function": "readSignal",
  "inputs": {
    "messageId": "0x18DAF110",
    "signal": "VIN",
    "timeoutMs": 500
  }
}
```

In this example the scheduler should not know how CAN works. It only knows that
`project.can` returned a `ModuleResult` with an outcome, outputs,
measurements, and optional error details.

## Guardrails

Use these rules when deciding where new code belongs:

| Rule | Meaning |
|------|---------|
| Protocol logic stays out of the scheduler | CAN/LIN/serial/DMM command parsing belongs to modules or transport hosts. |
| UI consumes read-only DTOs | UI should use ViewModel / ExecutionReport, not mutable runtime internals such as `NodeActivation`. |
| Scheduler owns orchestration | Retry, cleanup, loop, barrier, resource arbitration, and stop/abort behavior stay centralized. |
| Modules return neutral results | Business logic returns `ModuleResult`, not `NodeResult` or UI-specific data. |
| Configuration stays declarative | Flow, policies, resources, and module inputs should be represented in JSON/config, not compiled into the scheduler. |
| Variable replacement is shared | Paths and project variables should use `VariableResolver`, not one-off string replacement inside each feature. |
| Transport is replaceable | DLL, Python, QProcess, gRPC, and future hosts should fit behind `IModuleTransport`. |

## Current Maturity Snapshot

As of the current MVP, the project is directionally aligned with this vision:

| Area | Approximate maturity | Notes |
|------|----------------------|-------|
| Three-layer architecture direction | 80% | Boundaries are clear and now documented. |
| Scheduler core MVP | 70%-75% | Plan, session, retry, cleanup, barrier, resource, fixed for-loop control, report, and CLI are implemented. |
| JSON/configurable sequence flow | 75%-80% | Compiler, builder, schema, examples, warnings, validation, fixed loop flow, configuration variables, runtime loop variables, and structured measurements are in place. |
| Business module boundary | 82%-87% | `IModule`, `ModuleResult`, `IModuleTransport`, `QProcessTransport`, `PersistentQProcessTransport`, external host contract, Python script example, DLL ABI bridge, NativeHost DLL isolation, NativeHost manifest, measurement DTO, fake instrument host, and simulated CAN DLL proof exist. |
| Hardware session lifetime | 50%-55% | `DeviceSessionManager`, `IDeviceSession`, session factory abstraction, Station config JSON, `StationRuntime`, CLI `--station`, fake persistent instrument host, and module-backed cleanup exist; business-module device injection, production host protocol, and real drivers are still future work. |
| UI/ViewModel layer | 5%-10% | Intentionally deferred; should be built on DTOs after core contracts stabilize. |
| Product-ready platform | 35%-45% | Still needs persistence, richer diagnostics, UI, production reports, and hardware-in-loop project validation. |

These percentages are planning heuristics, not release gates.

## Near-Term Direction

The architecture-critical external process path is now represented by:

```text
ExecutionSession
  -> ActionNodeHandler
  -> TransportModuleAdapter
  -> QProcessTransport
  -> PicoATE.NativeHost.exe
  -> NativeHost manifest
  -> project C/C++ DLL
  -> ModuleTransportResponse
```

This path is now configurable through sequence `moduleBindings`,
`VariableResolver`, and NativeHost manifest JSON. A realistic software-only
CAN/DLL proof now exists:

- sequence JSON configures the test flow and CAN test item inputs
- NativeHost manifest configures the DLL loading boundary
- `PicoATE.CanExampleModule.dll` owns CAN byte decoding, measurement conversion,
  and min/max limit evaluation
- the UI and scheduler remain unchanged

This example does not require a CAN analyzer; it validates the framework
boundary before hardware is available. After that, the same boundary can support
real CAN drivers, instrument modules, and future transport variants without
touching scheduler orchestration.

Decoded values are now exposed as first-class `MeasurementResult` data through
`NodeResult`, `AttemptReport`, `StepReport`, CLI output, and external transport
response parsing. Loop body attempts also carry explicit iteration metadata, so
UI/report consumers do not have to infer loop iteration from repeated attempts.
The next design focus should be hardware module lifetime, NativeHost diagnostics,
and ViewModel shape.

Hardware lifetime has now started with `DeviceSessionManager`, Station config
JSON, `StationRuntime`, `PersistentQProcessTransport`, and a fake instrument
host: scheduler code still does not own VISA/vendor handles, while logical
devices can be configured from station files, loaded from the CLI via
`--station`, and exercised through a long-lived process that keeps state across
multiple step calls. The next step is to define how business modules access
logical device sessions and deepen the persistent host protocol before touching
real drivers.
