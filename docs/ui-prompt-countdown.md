# Prompt Countdown and Single-UUT Navigation

## Countdown

Timed operator prompts (OK, PASS/FAIL and input) use a compact header countdown
(clock icon and mm:ss, rounded up to seconds) in floating dialogs and Overview.
The old horizontal progress bar is hidden in compact mode. Response buttons remain
the last row, with equal widths and the original solid red/green judgment colors.
Confirmation and submission use the original blue fill in both presentations.
It refreshes at 100 ms intervals while visible, changes to amber/red near the
deadline, and stops ticking while hidden or after a response is submitted.

The initial remaining time uses the prompt request's UTC event timestamp and its
`timeoutMs` (default 60000), accounting for delivery delay. The UI records a
monotonic deadline on its private event copies. Those anchors survive repeated
events, switching between Details and Overview, and rebuilding the card layout;
language changes only update display text. Each new prompt instance gets a new
deadline, including a notice followed by a judgment using the same dialog key.
No countdown metadata is sent back to the Core or written into the script.

This is a display-only estimate of the existing prompt timeout, not another
timeout authority. At zero it shows "Awaiting timeout handling" and awaits the
engine's close event; it never submits FAIL or closes the prompt by itself.
The engine's registration can follow event publication by a small interval.
Pausing the workflow does not pause the monotonic deadline: the existing engine
policy remains unchanged.

- `timeoutMs <= 0`: "No timeout".
- `mode=notice`: "Closes automatically later" (后续自动关闭). Notice's
  short Shown-acknowledgment timeout is not its visible lifetime; after Shown,
  it remains visible until the configured closing step/condition or cleanup.
- Response accepted by the UI controller: "Response submitted"; the displayed
  countdown freezes until the engine confirms closure.

## Compact Overview Prompts

Each UUT prompt masks only its own card and centers a white panel (up to 360 px)
inside it. Shared once-per-batch prompts use a panel up to 480 px centered in the
visible cards viewport, not the potentially taller scroll content. Long content
scrolls inside the panel while the header and response buttons remain accessible.

A low-resolution background snapshot is cached once per displayed instance and
stretched with smooth filtering beneath a pale veil. Live cards are not blurred
every frame. The sharp interactive panel remains a separate child widget.
UI-side input drafts and pending-response state survive card rebuilds, language
changes and repeated presentation. Neither mask clicks nor background card clicks
submit a response. Core timeout, scheduling and plugin behavior are unchanged.

## Navigation

Admin and Test retain the Overview / UUT Details navigation row with one UUT.
As of V0.3.1, Overview is disabled when there is only one preview/result UUT;
the navigation row stays visible and UUT1 shows its normal tree and log. Reducing
the UUT count from multiple slots to one returns to Details, including after a
single-UUT run. Multiple slots re-enable Overview. Disabled slots still count as
configured slots, so disabling all but one slot does not hide the batch view.
The existing startup defaults remain: single
UUT details, multi-UUT overview where previously configured.

Station capacity rules for the top toolbar's UUT-count and Slots controls are
unchanged. The separate Admin presentation visibility switches are still honored.

## Verification and Deployment

The native-window regression tests cover remaining time, zero/no-timeout/notice
states, language changes, submitted responses, dialog-to-card migration, card
rebuilds, shared overlays, and repeated 1-to-2 UUT navigation in Admin and Test.
Existing prompt input, keyboard-dismissal and notice/judgment migration tests
are retained. No Core or plugin implementation changes are required.

Rebuilding the UI changes its SHA-256. Existing development-toolkit
`IntegrityBaseline.json` files are deliberately not silently reapproved by this
change. A daily administrator must review and approve the new UI hash before
using Test mode, or a controlled installer build can generate its new baseline.

Compact-layout validation on 2026-09-10: Debug and Release builds completed; each configuration
passed 17 focused native-window checks including fixture setup/teardown. The
existing Release Core/CLI/UI-runtime/integrity CTest selection passed 25/25.
Floating, four-card Chinese and shared-overlay screenshots were inspected. Tests
check compact clock transparency/spacing, panel bounds, long-content scrolling,
input drafts, pending states and the unchanged sidebar's 24-character default.
The short countdown unit scenario establishes its clock after cold widget setup
so Debug font/style initialization does not consume its test budget.

Debug testing exposed an input-method teardown callback accessing the destroyed
overview draft cache. The overview destructor now clears prompt callbacks before
QWidget tears down its children, including retired cards awaiting deferred deletion.
A regression explicitly sends a late input change during QObject destruction.
Language/input and prompt lifecycle checks additionally passed three consecutive
Debug runs after the fix.

Both development baselines still match Core and intentionally flag the updated
UI as Modified. No baseline approval, installer build, Git push, hardware run or
full-window-suite pass is claimed for this change.
