# PicoATE V0.3.2

## User Interface

- Admin and Test share the new overall-status design: explicit heading, state
  icon, light surface, colored left edge, secondary percentage and thin progress
  track. Elapsed time stays at the right of the Overview heading in smaller type.
- Batch progress sums completed/total steps for enabled UUTs. Progress survives
  retries and non-final report refreshes without counting periodic invocations.
- Cards retain their original contents, fonts and resource/periodic-task layout.
  Four use 2 x 2 and six use 3 x 2. Additional cards extend downward. Small
  windows scroll rather than hiding information or compressing the card contents.
- Per-UUT progress uses a full circular track with counts in the center and a
  final check/cross at two o'clock. No percentage is displayed inside the ring.
- Completed UUTs show a centered green/red result panel over a softened card.
  The small white UUT/SN identity is above PASS/FAIL. Click to dismiss; a new run
  clears previous results. Operator prompts retain priority over result panels.
- Navigation buttons show only UUT1, UUT2, etc. Their indicators are blue after
  a validated scan, yellow during the batch, and green/red on UUT completion.
  Disabled slots stay gray. SN values remain available in tooltips and reports.
- Scanner progress notifications support immediate updates after scan, undo,
  replacement or clear. They do not modify SN variables or engine execution.

## Distribution

- Application and installer version: 0.3.2.
- Installer: `out/installer/PicoATE-Setup-0.3.2-x64.exe`.
- Projects, logs, diagnostics and Debug artifacts are excluded from the package.
- Packaged UI/Core integrity hashes are regenerated from the release payload.
- Existing installed project data and routing configuration are preserved.
- The package includes the native host, CLI, register importer, Qt runtime,
  plugin DLLs and required Visual C++ runtimes, including GCAN's VS2013 runtime.

## Validation Scope

Release validation uses the Core/CLI/UI-model/integrity CTest selection, focused
native-window tests for the changed workflows, PE dependency checks and an
isolated-PATH startup/shutdown smoke test. No physical hardware validation or
full native-window-suite pass is implied.

### Recorded Results

- Release package dependency verification and isolated-PATH smoke test passed.
- All 53 manifest entries matched their hashes; the UI/Core baseline matched.
  The projects, log and diagnostics directories were empty.
- Focused native-window regression: 32 passed, including fixture cases.
- The 25-test CTest selection passed on recheck. The first invocation had one
  Core test process exit with `0xc0000409` and no individual failure output.
  One standalone rerun and three further runs from CTest's working directory
  each passed all 237 Core cases. The initial exception was not reproduced;
  its cause remains unconfirmed and should be tracked, not treated as fixed.
- Installer SHA-256:
  `2BD6F526A3C326ACB0445A12291FA1AE55DF8344AF127E59A4E4EED2DB358B96`.

The installed Inno Setup 7 compiler printed `Non-commercial use only` during
packaging. Confirm the organization's tool licensing before commercial delivery.
