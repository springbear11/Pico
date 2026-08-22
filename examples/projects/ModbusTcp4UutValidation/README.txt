PicoATE Modbus TCP four-UUT validation project

Purpose
  - One Run-lifetime MODBUS1 connection to 127.0.0.1:502.
  - UUT-1..UUT-4 use Unit IDs 0x01..0x04.
  - Setup opens once and Cleanup closes once.
  - The Main TestItem holds an exclusive MODBUS1 resource region so each
    UUT completes its read/check transaction before the next UUT enters.
  - The sequence only reads 0x6000..0x6003 and does not modify registers.

Run from this directory
  ..\..\..\PicoATE.Cli\PicoATE.Cli.exe run modbus_tcp_4uut_validation_sequence.json --uuts 4 --station StationSystem.json

Expected result
  - All four UUTs pass.
  - Each UUT reports its assigned Unit ID and [0,0,0,0] readback.
  - If a Unit ID is unavailable, only that UUT fails; Cleanup still runs once
    after the remaining UUTs finish.
