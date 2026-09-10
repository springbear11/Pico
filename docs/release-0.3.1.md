# PicoATE V0.3.1

## User Interface

- Admin and Test retain the Overview / UUT Details navigation row for a single
  UUT, but Overview is disabled. Reducing the count to one returns to Details;
  increasing it above one re-enables Overview. Disabled slots retain their identity.
- Operator prompts use a compact header countdown and centered content. Per-UUT
  prompts are centered within their own card with a softened background; shared
  prompts are centered over the visible card area.
- PASS/FAIL buttons retain their solid green/red palettes. Submit/OK use solid
  blue in both standalone and card prompts. Notices say "Closes automatically later".
- Prompt deadlines, input drafts and pending responses survive navigation and
  card rebuilds. Late input callbacks are detached before overview destruction.

## Distribution

- Application and installer version: 0.3.1.
- Installer: `out/installer/PicoATE-Setup-0.3.1-x64.exe`.
- Product projects, logs, diagnostics and development binaries are not bundled.
- The packaged UI/Core SHA-256 baseline is generated from the packaged binaries.
- Existing installed projects and routing configuration are preserved by setup.
- Qt, Visual C++ runtimes (including the GCAN VS2013 runtime), plugin dependencies
  and the register importer are verified by the packaging workflow.

## Verification

- Release and Debug UI builds completed. Each passed 17 focused native-window
  checks, including fixtures, single/multi-UUT transitions, disabled colors,
  single-UUT runs, prompt state retention and small-screen information layouts.
- The 25-test Core/CLI/UI-runtime/integrity CTest selection passed.
- Packaging checks cover nine registered plugins, PE dependencies, an isolated-PATH
  startup/shutdown smoke test, empty project/log/diagnostic directories and matching
  UI/Core SHA-256 values. No physical hardware or full-window-suite pass is claimed.
