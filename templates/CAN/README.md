# CAN Send And Read

GCAN and CX both expose `sendAndRead` as **Send And Read CAN Frame** in the
plugin palette. This adds no engine node kind, plugin ABI, or vendor SDK entry
point. It uses the existing transmit/receive implementations within one locked
`PicoATE_Execute` call. Existing `write`, `read`, and `requestResponse` remain
available.

## Configuration

Open the Station CAN device first. Select the same logical device for all
operations using that physical channel. Example Action:

```json
{
  "id": "can_exchange",
  "kind": "action",
  "moduleId": "plugin.can.gcan",
  "function": "sendAndRead",
  "timeoutMs": 65000,
  "inputs": {
    "deviceId": "CAN1",
    "id": "${var.CAN_TX_ID}",
    "data": "01 02 03 04",
    "extended": true,
    "remote": false,
    "rxId": "${var.CAN_RX_ID}",
    "rxMask": "0x1FFFFFFF",
    "timeoutMs": 1500
  }
}
```

For CX, use `plugin.can.cx`. Define the TX/RX variables with PerUUT scope when
each product has a different address. The example does not define any real
product command; replace the bytes according to the product protocol.

- `id`, `data`, `extended`, `remote`: same meaning as Send CAN Frame.
- `rxId`: required response ID. It need not equal the transmitted ID.
- `rxMask`: defaults to `0x1FFFFFFF`, comparing all 29 ID bits even for a
  standard response. A match satisfies `(receivedId & rxMask) == (rxId & rxMask)`.
  Zero accepts any ID and should not be used for independent UUT responses.
- Input `timeoutMs`: receive wait after a successful send, integer 1..60000 ms,
  default 1500 ms. The palette uses a 65000 ms outer step budget; Station/Host
  transport timeouts must also exceed the chosen receive wait.

All frame, response-filter, and receive-timeout parameters are checked before
transmitting. A transmit failure returns Error without receiving; no matching
response returns Timeout / `CanReceiveTimeout`. Error text and logs include TX
ID, expected RX ID, mask, channel and receive timeout.

Outputs preserve the existing receive fields (`id`, `idNumeric`, `data`,
`dataHex`, `dlc`, `extended`, `remote`, `timestampUs`) and add `transmitted`,
`txId`, and `txDataHex`. The receive measurement still uses `CAN_RX_FRAME`.
For parsing, select `${step:can_exchange.outputs.dataHex}` in fx.

## Boundaries

Serialization prevents another call in the same plugin Host from intervening
between the send and read. It is not a global hardware lock across independent
processes. Do not map one physical channel to multiple independently opened
Station devices/Hosts.

ID/mask matching follows the existing adapters. It does not validate a payload
sequence number, distinguish standard/extended frames sharing an identical
numeric ID, or prove that a matching frame already buffered by the driver was
caused by this request. It does not clear the channel buffer automatically;
nonmatching frames follow the existing Read filtering behavior. Use unique RX
addresses and protocol-specific validation for concurrent products. If multiple
replies share an ID, an additional payload correlation design is needed.

A Step retry resends the command, including after a receive timeout. Enable
retry only when retransmitting the product command is appropriate.

## No-Hardware Tests

```powershell
cd templates
cmake --preset vs2022
cmake --build --preset vs2022-release --target PicoATE_CAN_GCAN PicoATE_CAN_CX PicoATECanBridgeTests
ctest --test-dir .build/vs2022 -C Release --output-on-failure
```

The bridge test uses a fake adapter, covering standard/extended IDs, masked
matching, preflight errors with zero transmissions, TX/RX failures, timeout
diagnostics, legacy calls, and serialized concurrent exchanges. It does not
replace validation on the target USB-CAN hardware.

Validated on 2026-09-09: all seven bridge test scenarios passed in Debug and
Release; the existing 24 Core/CLI/UI-runtime CTest suites passed. NativeHost
Describe exposes the new function for both vendors in both build configurations.
The two CAN DLLs and their registry entries were updated in the UI Debug/Release
runtime directories; deployed DLL hashes match the builds. This CAN addition
does not change engine or UI production sources and has not been hardware-tested.
The September 9 same-version 0.3.0 installer also includes both updated CAN DLLs
and their sendAndRead descriptions.
