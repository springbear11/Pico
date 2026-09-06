# UI Language Switching

Admin and Test windows share a Chinese/English button in the Windows title bar,
immediately before the native minimize button. The initial language is
English. `QSettings` key `ui/language` stores `en` or `zh_CN` for the next launch.

`ui/src/UiLanguage.*` owns the Qt translator. The `UiShell` translation context
in `ui/src/translations/picoate_zh_CN.ts` contains UI display strings only.
Qt LinguistTools compiles the catalog and CMake embeds it in the executable's
resources. No separate translation file is needed on the target computer.

## Caption and Scanner Interaction

`ui/src/TitleBarLanguageButton.*` implements the caption control as an owned,
non-activating tool surface anchored to the native caption-button bounds.
The main window is not frameless: native resizing, snapping, minimize, maximize,
restore and close behavior are retained. The surface follows move, resize, DPI,
activation and visibility events, never uses global always-on-top, and hides
while a modal configuration window is open or another application is active.
Windows 11's native caption-color API matches the caption to the light UI.
Non-Windows/offscreen platforms keep the menu-bar corner fallback.

The control is a compact pill matching the login AUTO BY SN toggle: Chinese
shows only "中" in white on charcoal; English shows only "EN" in charcoal on
white. It keeps a 52-pixel logical width and is vertically inset in the caption.
The outer surface is transparent and has no native border or shadow. A 170 ms
fill transition provides click feedback without moving the window or stealing
focus from an editor/scanner. There is no continuous animation or UI polling.
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
- Plugin definitions, engine behavior, JSON files, log messages and report
  exports retain their original content. Login and remaining configuration
  dialogs are outside the initial translation coverage.

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
