# PicoATE

PicoATE is the first implementation slice of the ATE runtime described in
`architecture_v3_1.md`.

The long-term project goal and architecture guardrails are documented in
`docs/project_vision.md`.

The Qt UI architecture and phased development plan are documented in
`docs/ui_project_structure.md` and `docs/ui_development_plan.md`.

This repository currently contains the scheduler core and the first external
module integration slice:

- immutable `ExecutionPlan`
- runtime `ExecutionFrame`, `NodeActivation`, and `NodeAttempt`
- resource arbitration with serializable waiters
- barrier/cohort controller
- retry, cleanup, stop, and error decision handling
- fixed for-loop scheduler support
- JSON sequence compiler and CLI runner
- external module contract over stdio JSON
- Python script example through `QProcessTransport`
- NativeHost DLL isolation with manifest configuration
- software-only CAN DLL example through NativeHost
- station device configuration through `StationRuntime` and CLI `--station`
- persistent external process transport with a fake instrument host
- runtime device services for modules that use logical device ids such as `DMM1` and `CAN1`
- built-in DMM/CAN adapter spike modules backed by the fake instrument host
- independently generated Engine, UI, and combined VS2022 solutions
- asynchronous `ExecutionViewModel` and thread-safe `StopToken`
- Qt Widgets Runner with sequence/station selection, compile, run, stop, diagnostics, and basic results
- dedicated Diagnostic, UUT/Step, Attempt, Loop, and Measurement item models
- configurable UUT count for multi-UUT runs
- QtTest-based unit tests

The current UI milestone includes the UI-1 asynchronous execution boundary and
the UI-2 report models. It does not yet include live scheduler events, real
instrument drivers, session recovery, or production report rendering.

## Build With VS2022 And Qt6

Open this folder in Visual Studio 2022 as a CMake project and choose the
`vs2022-qt6` preset.

Command line:

```powershell
cmake --preset vs2022-qt6
cmake --build --preset vs2022-qt6-debug
ctest --preset vs2022-qt6-debug
```

The preset expects Qt at:

```text
D:/QT/6.9.1/msvc2022_64
```

### Visual Studio Solutions

The Engine solution remains independent and does not contain Qt Widgets UI:

```text
out/build/vs2022-qt6/PicoATE.sln
```

Generate and build the independent UI solution from `ui/`:

```powershell
cd ui
cmake --preset vs2022-qt6
cmake --build --preset vs2022-qt6-debug
```

```text
ui/out/build/vs2022-qt6/PicoATE.UI.sln
```

Generate and build Engine + UI together from the repository root:

```powershell
cmake --preset vs2022-qt6-all
cmake --build --preset vs2022-qt6-all-debug
```

```text
out/build/vs2022-qt6-all/PicoATE.All.sln
```

The dependency direction is always `PicoATEUi -> PicoATECore`. The Core target
does not link `Qt6::Widgets` and has no dependency on the UI source tree.

## Run A Sequence From The CLI

After building, run the CLI demo from the build output directory:

```powershell
.\out\build\vs2022-qt6\src\cli\Debug\PicoATE.Cli.exe run examples\simple_sequence.json
.\out\build\vs2022-qt6\src\cli\Debug\PicoATE.Cli.exe run examples\basic_sequence.json --uuts 2
.\out\build\vs2022-qt6\src\cli\Debug\PicoATE.Cli.exe run examples\custom_disabled_sequence.json
.\out\build\vs2022-qt6\src\cli\Debug\PicoATE.Cli.exe run examples\for_loop_sequence.json
.\out\build\vs2022-qt6\src\cli\Debug\PicoATE.Cli.exe run examples\external_echo_sequence.json
.\out\build\vs2022-qt6\src\cli\Debug\PicoATE.Cli.exe run examples\python_echo_sequence.json
.\out\build\vs2022-qt6\src\cli\Debug\PicoATE.Cli.exe run examples\nativehost_dll_sequence.json
.\out\build\vs2022-qt6\src\cli\Debug\PicoATE.Cli.exe run examples\can_dll_sequence.json
.\out\build\vs2022-qt6\src\cli\Debug\PicoATE.Cli.exe run examples\persistent_instrument_sequence.json
$env:DMM1_ADDRESS="USB0::0x0957::0x0607::MY59001234::INSTR"
.\out\build\vs2022-qt6\src\cli\Debug\PicoATE.Cli.exe run examples\simple_sequence.json --station examples\stations\basic_station.json
.\out\build\vs2022-qt6\src\cli\Debug\PicoATE.Cli.exe run examples\dmm_can_adapter_sequence.json --station examples\stations\basic_station.json
```

If no sequence path is provided, the CLI defaults to `examples/simple_sequence.json`.

`examples/python_echo_sequence.json` requires a Python interpreter. The CMake
build injects `PYTHON_EXE` automatically when `find_package(Python3)` succeeds;
outside the build tree, set a `PYTHON_EXE` environment variable.

The supported JSON fields and enum values are documented in
`docs/sequence_json_schema.md`.

Action module execution is documented in `docs/module_runtime.md`.

External process module contracts are documented in `docs/module_contract.md`.

Runtime placeholder replacement is documented in `docs/variable_resolver.md`.

NativeHost DLL manifests are documented in `docs/nativehost_manifest.md`.

Station device configuration is documented in `docs/station_config.md`.

`examples/can_dll_sequence.json` is a pure software CAN decode example. It does
not require a CAN analyzer; it validates the project module boundary before
real hardware or vendor drivers are available.

`examples/dmm_can_adapter_sequence.json` is also hardware-free. It uses
`examples/stations/basic_station.json`, built-in `example.dmm` / `example.can`
adapter spike modules, and `PicoATE.FakeInstrumentHost.exe` to validate the
logical-device path before real DMM/CAN drivers are available.

## Real CAN USB DLL Template

`templates/can_hardware_dll/` contains a standalone VS2022/Qt6 C++ DLL template
for a real vendor CAN SDK. It provides the PicoATE ABI, JSON routing for
open/status/write/read/requestResponse/close, persistent NativeHost session,
software loopback mode, manifest, sequence, and a Chinese integration guide.

Build and validate the software loopback first, then replace only the TODO
methods in `VendorCanAdapter.cpp` with the analyzer vendor API.
