# UI/Core Integrity Check

## Scope

This first version checks only `PicoATE.UI.exe` and `PicoATECore.dll` using SHA-256.
It does not check Station, sequences, Plugins, NativeHost, or other runtime DLLs.
The independent baseline file is `IntegrityBaseline.json` in the running toolkit
root, beside the UI EXE and Core DLL. Different Debug/Release toolkits have their
own baselines; no path inside a product project is used.

There are no digital signatures or encryption. A person with filesystem write
access can edit the baseline. This is a production-workflow safeguard, not
protection against deliberately patching the verifier or replacing the program.
It checks files on disk, not loaded code pages. After replacing program binaries,
restart the application. A corrupt UI/Core may prevent startup before self-check
is possible; this version does not add a trusted external launcher.

## Permissions and UI

- The fixed Admin password grants `AdminAccess::Standard`.
- The existing daily algorithm grants `AdminAccess::Supervisor`.
- Login selection carries the access level to MainWindow; it is not stored in
  QSettings and cannot be chosen in the GUI.
- Only Supervisor sees **Integrity Check** after Reports, localized as
  **完整性校验**. Standard Admin and Test have no such page.
- The page lists baseline/current hashes and status for the two required files.
  Modified/missing/unverified states require attention; mismatches stay red when
  selected. Hashes use two lines of 32 characters, with full values in tooltips
  and row copying via Ctrl+C.
- Refresh and approval hash in a cancellable worker thread. Closing the page
  requests cancellation and joins its worker before destroying it.
- Approval requires explicit row selection, a fresh daily-password confirmation,
  and a reason. Standard credentials are rejected by both UI and write helper.
- A missing/invalid initial baseline requires approval of both files. Missing or
  unreadable program files cannot be approved. For an established baseline,
  approval updates only selected files; other mismatches remain blocked.
- Files and baseline are rechecked against the reviewed snapshot before writing.
  QLockFile serializes writers; QSaveFile atomically replaces the JSON. The most
  recent 100 approvals retain before/after hashes, Windows user, time and reason.
  Passwords are not written to the baseline. Approval is disabled during a run
  in that Admin window; this is not cross-process administration.

## Test Execution

ProductionWindow enables the guard on ExecutionViewModel's RunRequest.
CoreExecutionService checks that request in its existing worker thread before
station run preparation, module registration, or an ExecutionSession begins.
The directory stays attached to the request across loop-test iterations, so
manual, scanned, auto-routed and repeated Test runs each recheck the files.
There is no timestamp-only cache or automatic baseline repair.

Missing, malformed, unreadable or mismatched baselines return a failed run-start
diagnostic with `executed=false`, before any test step runs. Test does not show
the Admin page; its existing error dialog directs the operator to a daily Admin.
Normal Admin debugging and CLI runs are not blocked by this first UI-only guard.
Core scheduler/plugin protocols are unchanged.

## JSON and Build Process

```json
{
  "schemaVersion": 1,
  "algorithm": "SHA-256",
  "updatedAtUtc": "2026-09-09T00:00:00.000Z",
  "updatedBy": "release-build",
  "files": [
    {"path": "PicoATE.UI.exe", "sha256": "<64 hexadecimal characters>"},
    {"path": "PicoATECore.dll", "sha256": "<64 hexadecimal characters>"}
  ],
  "history": []
}
```

The example hashes are placeholders, not an acceptable baseline. Packaging calls
`packaging/GenerateIntegrityBaseline.ps1` after deploying the final Release
binaries and before generating the package manifest. The independent JSON is
included in the toolkit root by the existing installer file rules. Upgrading the
complete installer refreshes both binaries and their release baseline.

Normal CMake builds do not silently replace a previously approved baseline.
For an initial development toolkit, use the helper with `-RuntimeDirectory` only
after the build has completed. Overwriting an existing baseline requires explicit
`-Force` for a controlled release build, or Supervisor approval in the UI.

GUI tests use a temporary synthetic runtime directory via a test-build-only
`integrityTestRoot` property. That override is compiled out of the actual UI EXE.
The standalone `PicoATEIntegrityTests` covers roles, required files, missing and
invalid baselines, selected updates, stale reviews, locked/cancelled writes,
out-of-scope files, and repeated run-start checks.

## Validation (2026-09-09)

- Debug and Release builds completed. Initial baselines were generated once in
  both UI runtime directories and both file digests were independently checked.
- `PicoATEIntegrityTests`: 11 passed in each configuration (including fixture
  setup/teardown).
- Focused native-window checks: 9 passed in each configuration. Includes the real
  LoginDialog credential classification, page visibility, reauthorization,
  selected-file updates, Test rejection without a baseline and successful retry
  after approval, unchanged yield counters for a rejected start, scanner batches
  and loop testing.
- Release CTest excluding the full window suite: 25/25 passed.
- English 1100x620 and Chinese 850x560 page screenshots were inspected; selected
  mismatches retain red text and both halves of each hash remain visible.
- An additional existing `productionWindowPreloadsFlowAndRunsWithoutScanner`
  test fails its `headerMatchesRunColumns()` assertion before triggering Run,
  including an isolated rerun. This task does not change that header/sidebar
  layout or weaken the assertion. Manual-run integrity behavior was verified
  separately by `productionIntegrityBlocksUntilBaselineIsApproved`.
- The September 9 same-version 0.3.0 installer includes this feature and a matching
  root-level baseline. Its 53-file package manifest and both baseline hashes were
  independently verified, with empty projects and a passing isolated-PATH startup.
  No hardware validation or full-window-suite pass is claimed.
