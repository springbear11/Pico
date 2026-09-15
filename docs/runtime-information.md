# Runtime Basic Information

Admin and Test share an icon-only configuration action in the Overview station
information area and in the UUT Details product-information heading.

- Customer ID, Order, Tester and Jig No. start empty (`--` in summaries) in each
  new window/session, regardless of legacy Station values or QSettings history.
  They remain in memory until the window exits, including project changes.
- Apply remembers these four values in `RunInformation/Previous` in QSettings.
  They are restored only by explicitly choosing Load Previous. Cancel does not
  update history or active values. Model and Station ID are never imported.
- Station ID is the local computer name and is read-only in both the new dialog
  and Station Config. The UI document/compile snapshot normalizes old or missing
  identifiers to the computer name without marking the document dirty on load.
  A Station Config save records the computer name; GUI runs
  always use their captured computer name rather than a legacy Station ID.
- Model is read from the selected Station. The dialog atomically updates only
  the model key, preserving devices, rules and other configuration. In Admin,
  changing Model requires a saved Station with no pending edits; reloading it
  uses the existing compilation invalidation behavior. Templates can still be
  configured and saved through Station Config.
- Configuration is disabled during active runs. A copy of all six fields is
  captured in the UI run request and reused by every UUT and loop iteration.
  Canonical run variables (`stationId`, `model`, `customerId`, `order`, `tester`,
  `jigNo`), TXT headers and report metadata share this snapshot, including blanks.
- The scanner yields to this modal dialog without discarding its current draft
  or collected SNs. Import and editing happen inside the dialog before Apply.

The execution scheduler and plugin interfaces are unchanged. Requests without
the optional UI information snapshot retain legacy service/CLI metadata behavior.
Existing Station metadata remains readable for compatibility, but is not used as
the GUI session's runtime default.

Customer ID, Order, Tester and Jig No. are no longer editable in Station Config.
Its basic-settings save leaves legacy customer/metadata keys unchanged and does
not create them in new files. Configure these fields through Basic Information
only. CSV, XLSX and PDF share the report metadata snapshot; TXT headers use the
same active-run snapshot. Empty runtime fields do not fall back to old Station
values. This does not change the legacy CLI configuration contract.
