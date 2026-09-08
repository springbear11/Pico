# UI Language Switching

Admin and Test windows share a language icon at the far right of the main
controls toolbar. It opens a menu of supported languages with the current
language checked. The initial language is
English. `QSettings` key `ui/language` stores `en` or `zh_CN` for the next launch.

`ui/src/UiLanguage.*` owns the Qt translator. The `UiShell` translation context
in `ui/src/translations/picoate_zh_CN.ts` contains UI display strings only.
Qt LinguistTools compiles the catalog and CMake embeds it in the executable's
resources. No separate translation file is needed on the target computer.

## Language Menu and Scanner Interaction

`makeLanguageButton()` in `ui/src/UiTextBinding.cpp` creates a normal child
QToolButton with a Lucide language icon and QMenu, not a combo box or a floating
title-bar surface. It stays visible when another application or the taskbar
takes focus. The native window frame is unchanged. Hover and menu-open
backgrounds provide feedback. The former TitleBarLanguageButton is not used by
either application window. New supported locales can be added to the menu and
translator without changing the toolbar layout.
READY/Ready display as "待开始".

Admin and Test both hide the scanner during UUT Slots configuration and restore
it only if it was visible before opening Slots. Restoring does not reset the
batch: committed barcodes and physical slot numbers are retained; cancel also
retains the input draft. Production scanner callbacks recheck configuration
state and runtime readiness at dispatch time to prevent late popups.

## Scope

- Workspace tabs, menu and toolbar commands, common shell labels and tooltips.
- Run Test headers, displayed states, Overview cards, retry, resources, periodic
  tasks, yield and batch summaries.
- Scan controls, UUT slot controls and default operator-response controls.
- Editor section titles and table headers. Technical parameters and their
  values remain in English, including address, canId, frame, kind, onFail and
  executionScope. User-provided step names, messages, SNs and IDs remain intact.
- Basic-function palette tooltips describe the actual purpose in both languages;
  external plugin descriptions stay in English.
- Station Basic Settings labels, Product Routing and Test device configuration
  are localized. The Station device-parameter editor is unchanged.
- Plugin definitions, engine behavior, JSON files, log messages and report
  exports retain their original content. Login is outside this coverage.

## Overview and Compact Displays

### Station Capacity and Dialog Safety

Station `uutCount` is the default and maximum toolbar count in both windows.
The toolbar may temporarily reduce it; it never writes that reduction into
Station or restores it from the old `MainWindow/UutCount` preference. Opening
a Station or changing its capacity resets the default. A capacity of one hides
both count and slot controls. An unchanged capacity preserves a temporary
reduction while other Station metadata is saved.

Auto routing applies the matched Station's capacity. A reduced count is kept
only for the same Station, not carried over to a different product. Physical
slot indexes and engine request semantics remain unchanged.

`ScanDialog` is an owned non-modal window, without system-wide always-on-top.
It keeps requested visibility separate from temporary suspension, hides for
modal dialogs that block its owner, and restores only after all blockers close.
Qt window-blocked notifications also cover native modal windows. Draft text,
selection and committed barcodes survive suspension. Explicit hiding, cancel,
and starting a run cancel any pending restoration. Callers use
`isScanRequested()` to avoid resetting a suspended batch in a late callback.

Responsive sizing uses available sidebar height as well as window size, with
hysteresis to avoid oscillating between layouts. A restored derived layout-mode
cache no longer suppresses sizing on new controls. Information rows keep their
font-metric height; a scrollable sidebar provides a fallback when the available
height is insufficient instead of clipping glyphs.

Admin preserves the complete physical-slot list for compile previews and each
run iteration. Disabled slots remain gray, non-interactive cards and navigation
buttons; they are not submitted for execution or included in result counts.

The Overview summary uses the same result colors as the single-UUT sidebar,
with larger status and elapsed-time text. Compact sidebars reserve sufficient
width for labels and reduce metric spacing and chart sizes.

`ui/src/ElidedInfoLabel.h` renders SN and other product fields on one line,
showing at most 24 characters plus an ellipsis (or fewer if the cell is narrow).
The complete original value remains in QLabel::text(), is shown on hover, and
can be copied from the context menu. No newline, truncation, or formatting is
written back into a variable, log, report, or configuration document.

## Maintenance

Add display-only text to the `UiShell` catalog and use `uiText()` at the display
boundary. Static widget text should use `bindUiText()` or the small creation
helpers in `UiTextBinding.*`, so existing controls update immediately.
Dynamic state labels must refresh on `UiLanguage::languageChanged` as well as
runtime events. Use `uiStateText()` only with known state tokens, never arbitrary
user text or a value that will be serialized.

Do not rebuild the window, reload the script, commit drafts, reset models or
reconfigure prompts when switching language. Model display notifications retain
persistent indexes and selections. UI styling and execution decisions must use
the state enums, not translated text. Do not change the numeric locale with the
display language.

The language regression tests in `MainWindowLifecycleTests.cpp` cover pending
Flow edits, active multi-UUT runs, scanner contents, model indexes, unchanged CSV
exports, prompt inputs/responses and physical slot choices. Build both Debug and
Release after updating the catalog.

## Validation (2026-09-08)

Station-capacity/modal follow-up:

- Release: 28 focused window checks passed, including fixture setup/teardown.
- Debug: 9 new checks passed, including setup/teardown. They cover Station
  capacities 1 and 4, ignoring a legacy cached count, temporary reductions,
  live capacity changes, nested dialogs, cancellation, and the actual Station
  save-confirmation workflow with a partially scanned batch.
- Native file-dialog coverage waits for its Windows HWND to be visible before
  cancellation; the original immediate-startup-cancel test hit a qwindows
  worker-thread crash. The realistic visible-dialog path passed in Release
  and Debug, alongside synthetic native window-block/unblock notifications.
- An additional 1.25x Qt scale passed the normal/maximized/fullscreen/restored
  window checks with Chinese captions and full-height information rows.
- The CLI/Core/UI-runtime Release CTest selection passed (24/24). Engine and
  plugin sources are unchanged; this follow-up does not rebuild the installer.

- Debug and Release UI builds completed with 553 finished translations.
- Release focused window checks: 18 passed, 0 failed, including fixture setup
  and teardown. Covers disabled slots before and after a run, physical UUT IDs,
  the language menu, configuration drafts, English plugin descriptions, and
  single-line 22/24/32-character SN display with full-value hover text.
- Small-screen screenshots were inspected at 1100x680 and 1366x768 logical
  window sizes. SN caption and content share the same non-wrapping form row;
  the SN value aligns with Station ID, Model, and Customer ID values.
- Additional 1.5x Qt scale: 4 passed, 0 failed, including setup/teardown.
- UI runtime CTest suite passed. No engine or plugin source changes.
- Debug expanded window checks: 12 passed, 1 existing failure. The assertion
  in `adminMultiUutRunShowsOverviewAndNavigatesToDetails` about two-card vertical
  centering has the same geometry as the pre-change September 6 log (card center
  355, host center 266). It remains recorded, not removed or weakened.
- These are focused regression checks, not a full hardware or all-window
  validation. The 0.3.0 installer is refreshed after the value-column alignment
  follow-up, without changing the version number.

## Validation (2026-09-06)

- Debug and Release UI builds completed with 428 finished translations.
- All five language-switch scenarios passed in both build configurations.
- UI runtime suite: 96 passed, 0 failed.
- Focused window regression: 23 passed, 0 failed, including fixture setup and
  teardown. Covers responsive layout, scanner replacement, routing, stop,
  cleanup, retry, card prompts, and shared prompts.
- The variable Type/Scope column-width regression passed using the native
  Windows platform in Release.
- Log and debug models refresh only their headers on a language change; their
  untranslated cell contents do not require a full view refresh.
- Caption/Slots follow-up: 16 focused scenarios passed in Debug and Release
  (18 including fixture setup/teardown), including six Admin/Test scanner
  accept/cancel/hidden cases, real native mouse input, native window states,
  the five translation cases, Devices, slot configuration and small-screen UI.
- The Release native-caption test also passed with an additional 1.5x Qt scale
  factor. This is a local scaling check, not a substitute for testing every
  Windows version or physical multi-monitor arrangement.

These are focused validation results, not a claim that every window test was
rerun. Earlier offscreen runs included layout-geometry assertion failures; the
existing two-card centering assertion is outside this change's scope.
