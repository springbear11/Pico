# PicoATE Installer 0.3.3

## Versions

- Installer/distribution version: 0.3.3.
- UI, Core and the nine self-developed plugin DLLs: 1.0.0.
- Installer: `out/installer/PicoATE-Setup-0.3.3-x64.exe`.
- Installer versioning is independent of component file versions.

## Changes

- Admin and Test share Basic Information configuration in Overview and UUT
  Details. Customer ID, Order, Tester and Jig No. start empty for each session.
  Previous values are imported explicitly from QSettings, not loaded silently.
- Station ID is the readonly computer name. Model persists in Station. Runtime
  fields are removed from Station Config and new-project defaults; legacy files
  remain compatible without supplying stale values to UI reports.
- Variables, TXT logs and CSV/XLSX/PDF reports use the run's captured information.
  Configuration is unavailable during a run; the scanner yields to its dialog.
- Integrity Check displays baseline/current component versions and SHA-256.
  It covers UI, Core and `plugins/**/PicoATE.*.dll`, not third-party libraries,
  Station or sequence files. Legacy vendor baseline entries are ignored.
- New, replaced and removed PicoATE plugins require explicit authorization.
  Scanning, Test/Admin run start and device operations enforce the inventory.
  Approvals record selected file/version/hash changes and a reason. UI prompts
  do not describe the credential algorithm. No digital signatures are added.
- Station Type is derived from plugin filenames, deduplicated and shared with
  Driver / Model filtering. Existing Station types remain available for recovery.

## Distribution and Validation

- Packaging regenerates the approved baseline from the final release payload.
- Projects, logs, diagnostics and development artifacts are excluded; installed
  project data and existing routing configuration are preserved.
- NativeHost, CLI, RegisterImporter, the Qt runtime, plugins and required vendor
  dependencies remain included, including the GCAN VS2013 runtime.
- Release validation uses the 25-test Core/CLI/UI-model/integrity CTest selection,
  targeted native-window regression, dependency verification and an isolated-PATH
  startup/shutdown smoke test. No physical hardware validation or complete
  native-window-suite pass is implied.

## Recorded Results

- All 25 selected CTest targets passed.
- Focused native-window regression: 49 passed, including fixture cases.
- Portable dependency verification and isolated-PATH startup/shutdown passed.
- All 53 package-manifest entries matched their file hashes. The independent
  baseline contained exactly 11 governed files, all at component version 1.0.0.
  Projects, log and diagnostics directories were empty.
- Installer file version: 0.3.3; size: 24,882,890 bytes.
- Installer SHA-256:
  `679D93E9D2929BD498055CBF87A17AC5AEC939826CF99608D67085897D7E23CA`.

The installed Inno Setup 7.0.2 compiler printed `Non-commercial use only` during
packaging. Confirm the organization's tool licensing before commercial delivery.
