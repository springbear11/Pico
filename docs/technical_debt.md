# PicoATE Technical Debt

## Execution Report Enhancements

`ExecutionReport` now provides the first read-only result DTO for CLI, reports,
and future UI ViewModels.

It currently exposes:

- session state and completion/error flags
- UUT-level `hasError`
- step display name, kind, activation state, final outcome, and `wasError`
- attempt index, outcome, error code, error message, and structured measurements

Future result/reporting needs:

- structure skip reasons instead of inferring them from messages
- expose cleanup activation reason
- expose resource wait/acquire/release history
- decide whether `StepReport::measurements` is enough for UI/reporting or if a
  separate flattened measurement table should be generated for database export

## PlanBuildResult Ownership

Current `PlanBuildResult` stores `ExecutionPlan plan` by value.

This keeps the early API simple and pleasant to test, and Qt containers are
implicitly shared, so the cost is acceptable for the current MVP.

Potential future issue:

- large production sequences may contain hundreds or thousands of steps
- `SequenceCompiler` may compile many sequences in batch
- `PlanCache` may want to store immutable plans directly

Planned follow-up:

- keep the value-based API until profiling or integration pressure says
  otherwise
- if this becomes a bottleneck, prefer
  `std::shared_ptr<const ExecutionPlan>` over a mutable out-parameter so the
  immutable-plan semantics stay explicit

## SequenceCompiler Unknown Field Strict Mode

`SequenceCompiler` now reports unknown JSON fields as warnings while preserving
`CompileResult::ok()` for otherwise valid sequences.

Open questions:

- should strictness be configurable per station/project?
- should warnings eventually carry stable diagnostic codes?
- should CLI support `--warnings-as-errors` for CI or release gates?

Planned follow-up:

- add an optional strict mode or `warningsAsErrors` setting
- keep `x-*` fields and a `vendor` object as extension namespaces

## Real Project Module Proof

The external module path is proven with mock host, Python echo, test DLL,
NativeHost, NativeHost manifest, and a pure software CAN DLL example.

Completed:

- `PicoATE.CanExampleModule.dll` implements the existing `PicoATE_Execute` ABI
- `examples/nativehost/can_decode_manifest.json` configures DLL loading
- `examples/can_dll_sequence.json` invokes it through `moduleBindings`
- decoded values return through `outputs` and `measurements`
- UI and scheduler orchestration code did not need project-specific changes

This validates the main delivery promise: project-specific behavior lives in
business modules and JSON/config, not in framework code.

Remaining follow-up:

- replace the simulated CAN DLL with a real project DLL when hardware or vendor
  SDK access is available
- keep real CAN acquisition, DBC/ARXML parsing, and protocol-specific logic
  outside the scheduler
- use the CAN example to drive richer reporting/export formats on top of the
  implemented `MeasurementResult` DTO

## NativeHost And QProcess Hardening

`QProcessTransport` currently starts a short-lived process per module call and
maps success, timeout, and process errors into transport status.

Planned follow-up:

- capture and truncate stderr/stdout diagnostics for failed hosts
- distinguish launch failure, protocol parse failure, non-zero exit, and
  timeout more clearly
- consider a long-lived host or process pool for high-frequency module calls
- add protocol version metadata to request/response or manifest
- keep legacy `--dll` support, but prefer manifest mode for project delivery

## Hardware Session Lifetime

`DeviceSessionManager` now provides the first core abstraction for logical
devices and connection reuse. Station config JSON can parse logical device
configuration, `StationRuntime` owns the station/device context, and CLI
`--station` can load the configuration before a sequence run.
`PersistentQProcessTransport` and `PicoATE.FakeInstrumentHost.exe` now prove
that a host process can keep state across `open -> read -> read -> close`
calls. Module-backed cleanup is also supported, so hardware shutdown can live
in cleanup instead of being silently treated as no-op cleanup.

Remaining work:

- decide how `ExecutionSession` receives or references `StationRuntime`
- wire station config into later UI startup flows
- inject device access into business modules without exposing scheduler internals
- deepen the persistent instrument host protocol with health check, reconnect,
  and explicit shutdown semantics
- replace the fake persistent host with real DMM/CAN/PSU drivers when hardware
  and vendor SDK access are available
- connect ResourceManager ownership with DeviceSessionManager session state
