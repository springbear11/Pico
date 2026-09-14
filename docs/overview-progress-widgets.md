# Overview Progress Widgets

The 2026-09-14 preview retains the existing summary layout, including the left
batch status area and right-aligned total elapsed time. No engine or plugin
execution behavior is changed.

## Batch Progress

The status area follows preview A: a light surface, a narrow colored left edge,
an explicit Overall Test Status heading, a status icon and prominent state text.
The percentage sits to the right; Total Progress uses a separate 6px bottom
track. The surrounding station fields and UUT cards are unchanged. Progress is the sum of completed
steps divided by the sum of total steps for enabled UUT slots, not an average
of UUT percentages. Empty totals display 0%. Running uses yellow accents;
completed/failed accents use the same deep green/red as the result overlays.
Both modes share the widget and translated headings. Icons reuse the existing
function/icon resources and are cached on state changes, with no added timer.

Counts reuse the Overview model's planned step nodes (including reported
containers and their children). Terminal nodes, including skipped nodes, count
as completed flow. Periodic invocation events do not add repeated completions.
Within a run, completed node IDs are remembered across retries and non-final
report refreshes; starting a fresh preview/run resets them. Final reports retain
their actual terminal-step counts. Completion percentage is not pass yield.

## Per-UUT Ring

Each card has a fixed 112px full circular track in place of the old horizontal
bar and standalone percentage. Its center retains `completed / total` even after
completion; no percentage text is shown in the ring. The arc follows the same
count ratio. A terminal pass shows a small green check embedded in a gap at the
two-o'clock position on the circumference; failed or stopped UUTs show a red
cross there. There is no central result icon or separate badge. Disabled slots
show a gray track and `--`.

The track and progress arc are thinner (3.5px and 4px). Static translucent layers,
a subtle rim highlight and a faint lower edge give a lightweight frosted-glass
appearance. This is painted material styling, not real-time background blur.

## Card Result Overlay

A terminal UUT shows a borderless, solid green PASS or red FAIL panel centered
inside its own card, capped at 300 x 144 logical pixels. Surrounding card content
uses the same softened, tinted snapshot as operator prompts. The snapshot is
captured after the card is laid out, once per presentation, not on a timer or
every runtime event. Stopped UUTs use
FAIL; disabled slots do not show a result.
A centered 12px white `UUT-1 - SN` line sits directly above the large result.
When SN is empty, only the UUT identifier is shown. Long identities are elided
within the panel and remain complete in its tooltip and accessible name.
The large white bold label follows the selected UI language. Clicking anywhere
on the result panel or surrounding card dismisses the overlay only: it does not navigate to Details or
submit an operator response. The next ordinary card click retains its normal action.

Dismissal is remembered per UUT/outcome across report rebuilds, page switches and
language changes. A late PASS-to-FAIL correction is shown again. New-run/scan
preview resets clear the overlays and dismissals, including when Overview is
hidden. Simply opening the scanner or an invalid scan does not discard results.
Existing operator prompts remain above result overlays and are not auto-confirmed.

The original footer counts, error code, duration, retry attempt, recent steps,
resource state and periodic-task status remain. Ring painting is event-driven;
no animation timer, live blur or added hardware polling is introduced. The ring
is transparent to mouse events so clicking it still opens that UUT's details.

## Responsive Grid

- Two UUTs remain a centered pair. Four use 2 columns x 2 rows; six use
  3 columns x 2 rows. Three use 2 + 1, and five use 3 + 2, in UUT order.
- Six is the normal two-row capacity. More than six retain three columns and
  extend downward with vertical scrolling. Disabled slots retain their positions.
- The original card contents, fonts, captions, 112px ring, recent steps, retry,
  resource badge placement, periodic task/last result and footer are restored.
  There are no compact/tiny modes and no viewport-driven hiding or rearranging.
- Only the outer card dimensions follow the viewport. Content size hints set a
  lower bound, so very small windows scroll rather than clipping, shrinking
  fonts, or hiding information. This takes priority over fitting all six on a
  physically insufficient screen. Resizing does not recreate cards/prompt drafts.
- Native Admin/Test checks cover both languages, 2-12 slots and window resize/
  maximize/restore, explicitly asserting unchanged ring/font sizes, visible
  footer/task captions and the original resource placement.

## Verification

- Release application and native-window/model test targets build successfully.
- Focused native-window checks cover pixels of the fill and full ring, result
  marks, zero totals, mixed UUT totals, disabled slots, retries, retained counts,
  single-UUT navigation and prompt rehosting.
- The 25-test Core/CLI/UI-model/integrity CTest selection passes, including the
  new report-refresh count regression.
- Native screenshots show running, passed, failed and disabled example UUTs;
  their sample values are test fixtures, not hardware results.
- Development integrity baselines are not silently reapproved. No installer,
  Git push or full native-window-suite pass is implied by this preview.
