# Software Versions and Plugin Approval

- UI starts at 1.0.0, Core at 1.0.0, and the self-developed plugin bundle at
  1.0.0. Windows file-version resources are embedded at build time. Vendor
  dependency DLLs are excluded from version and SHA-256 approval checks.
- `IntegrityBaseline.json` remains beside the executable. Schema 2 records a
  relative path, file version and SHA-256 for UI, Core, and `PicoATE.*.dll` files
  recursively below `plugins/` (case-insensitive). Baseline entries for missing
  PicoATE plugins remain visible until an authorized supervisor retires them.
  Legacy schema-2 vendor entries are ignored even if their files are changed or
  missing; subsequent approval writes only the controlled files to the active
  list. Existing authorization history is retained.
- Inventory is discovered from disk, not only PluginRegistry.json. Unregistered
  new PicoATE DLLs therefore cannot silently enter an approved installation. Referenced
  plugin DLLs must be in the approved inventory, not an external folder.
- Only an authorized supervisor can see the page and authorize changes.
  Authorization requires the authorization password again and a reason. Selection is
  explicit: approving one file does not approve other additions or replacements.
  Version/hash changes and plugin additions/removals are recorded in history.
- Schema 1 is not silently upgraded. Review and select all files to establish
  the expanded baseline. Missing UI/Core and unreadable/linked paths cannot be
  approved. Station and sequence files remain outside this feature's scope.
- Verification runs off the UI thread. Test start, authenticated Admin runs,
  device connection tests, CAN discovery and the Admin plugin scanner enforce
  the check before loading plugin code. The scanner never approves files itself.
- Release packaging generates schema 2 only after collecting component files;
  portable verification checks the PicoATE inventory, versions and hashes.
  Ordinary packaging dependency checks/deployment still include vendor libraries.
  Building a local executable does not silently overwrite an administrator's baseline.

This is deployment control, not a signature or a sandbox. Someone with write
access can still edit the JSON baseline or run the standalone CLI/NativeHost
outside these UI gates. Files must not be replaced while a batch is running.

## Version Locations

- UI: root `CMakeLists.txt`, standalone `ui/CMakeLists.txt`, UI fallback in
  `ui/src/Main.cpp`, and packaging defaults.
- Core: `picoate_component_version` in `src/core/CMakeLists.txt`.
- Plugins: `templates/CMakeLists.txt` project version and adapter descriptions.
  The plugin resource template lives under `templates/common/` so the plugin
  project remains independently buildable.

## Station Device Types

Station Type choices follow the registered plugin DLL filenames. For example,
`PicoATE.CAN.GCAN.dll` yields `CAN`, and `PicoATE.DMM.KEYSIGHT34410A.dll` yields
`DMM`. Types are uppercase, deduplicated and sorted. Driver choices in both the
device table and the property editor use the same filename-derived type.
Older nonstandard native registry descriptions retain their category fallback;
types already used by the Station remain selectable when a driver is missing.
After authorizing a new plugin, Scan Plugins refreshes these choices without a
UI code change.
