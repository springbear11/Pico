# UUT Navigation Status

The Overview navigation row shows UUT1, UUT2, etc. SN values remain in tooltips,
the cards, details, variables, logs and reports; they no longer affect button width.

Each button uses an Overview-style circular indicator and a separate light
selected background/border:

- Empty scan: hollow dark circle.
- Validated SN stored for this slot: blue fill.
- Active batch (including pending slots, pause and stopping/cleanup): yellow fill.
- UUT completion: green PASS or red FAIL/Stopped. Retry is not a final result.
- Disabled slot: disabled gray button/indicator; its physical index is preserved.

ScanDialog emits a UI-only batchProgressChanged notification after validated
values or slot enablement change. Draft typing, invalid/duplicate SNs and merely
moving between slots do not mark a slot scanned. Undo, replacement and clear
update the indicator; merely opening a fresh empty scanner preserves the previous
results until the operator begins editing the new batch.

UutNavigationStatus only observes the existing view model and scanner. It neither
edits a run request nor assigns SNs or results. Live model updates refresh the
affected buttons without rebuilding them. Final UUT outcomes take precedence over
continuing batch work and background periodic tasks. Admin and Test share the same
presentation code. No engine or plugin changes are required.
