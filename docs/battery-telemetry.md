# Battery telemetry (`G80BAT`)

The dongle prints one line every `CONFIG_GLOVE80_BATTERY_HEARTBEAT_MS` (default
10 s) to its USB CDC serial log (`/dev/cu.usbmodem*` on macOS). The line reports its
cached view of both halves' battery. The consumer is
`hammerspoon/keyboard-battery/` in the `Laptop-Config` repo.

**This format is a contract with that consumer.** Its parser accepts only `v=1`.
If you change a field name or its meaning, bump `v` and update the host parser at the
same time. Otherwise the host logs a parse error and stops collecting.

## Format

```
G80BAT v=1 up=109662 fw=20260923T1412-591f01d l=85 la=3421 r=72 ra=51 split=2
```

Each line has the `G80BAT` prefix followed by `key=value` tokens separated by single
spaces. No value ever contains whitespace. Every field is always present.

| Field | Meaning |
|---|---|
| `v` | Format version. Currently `1`. |
| `up` | Dongle uptime in ms. It drops back near zero when the dongle reboots. |
| `fw` | Build id: `<UTC configure time YYYYMMDDTHHMM>-<git short sha>[-dirty]`. The sha is `nogit` if git was unavailable at build time. |
| `l` / `r` | State of charge in %, 1–100, or `na`. |
| `la` / `ra` | ms since that side's % last **changed**, or `na` when the % is `na`. |
| `split` | Number of halves with a live BLE link to the dongle (0–2). |

## Semantics

- **Unknown is `na`, never `0`.** A side shows `na` until its first reading arrives
  and again whenever it is disconnected. ZMK signals a disconnect as level 0, so a
  real 0% reading also shows as `na`.
- **The age resets only on a change.** A side's "last changed" stamp moves only when
  a report differs from the cached %. Repeated or identical reports, including the
  read the dongle makes when a half reconnects, leave it alone. The host can place
  each 1% step in time as `now - age`.
- **The cache survives idle and reconnects.** An idle half sends nothing, and its
  value and age carry on. A disconnected half shows `na`, but the dongle keeps its
  value and stamp. If it reconnects at the same %, the age continues where it left
  off.
- **The age can undercount.** It measures time since the dongle *learned* the
  current value:
  - After a dongle reboot, the first value is stamped when it arrives, so `la ≤ up`.
  - After a reconnect at a different %, the step is stamped at reconnect time.

  In both cases the real step happened earlier than `now - age`.
- **The level is known right after connect.** The central reads each half's Battery
  Level characteristic as soon as the half connects, so a freshly booted dongle
  shows real values within seconds.
- **`l`/`r` follow pairing order, not physical side.** They are ZMK's split slot
  indices 0 and 1, assigned in the order the halves first paired with the dongle.
  They match left/right only if the left half paired first after the dongle's
  settings were last reset.
- **Resolution is 1%.** ZMK's split link carries only state of charge. Voltage
  would need a custom split GATT characteristic.

## Cost

Reading is free. The line is built from state the dongle already holds, on
USB-powered hardware. The halves keep their stock behaviour: sample every
`ZMK_BATTERY_REPORT_INTERVAL` (60 s) while active, nothing while idle, and notify
only when the % changes. Nothing the host does makes the halves do extra work. Do
not lower the halves' report interval to get finer resolution. The age fields
already provide it.

## Configuration

In `config/glove80_dongle.conf`:

```
CONFIG_GLOVE80_BATTERY_TELEMETRY=y        # enable the heartbeat and shared cache
CONFIG_GLOVE80_BATTERY_HEARTBEAT_MS=10000 # interval, 1000–600000
```

The host waits up to two intervals per read, so a longer interval makes each read
slower.

The dongle logs at INFO (`CONFIG_ZMK_LOGGING_MINIMAL=y`). At DEBUG the port drops
messages during typing bursts, which can swallow heartbeat lines, and it writes a
keystroke stream to a world-readable device node. The heartbeat is printed with
`printk`, so it is not affected by the log level.

## Coupling: keyboard name

The host finds the dongle's serial device by looking up `CONFIG_ZMK_KEYBOARD_NAME`
(`"Glove80"`, set in `boards/shields/glove80_dongle/Kconfig.defconfig`) in the
IORegistry. Renaming it breaks the host silently.

## Code

- `src/battery_telemetry.c` (with `include/glove80/battery_telemetry.h`) owns the
  battery cache and the heartbeat.
- `boards/shields/glove80_dongle/info_dump.c` reads the same cache.
- The module is wired up in the root `CMakeLists.txt`, `Kconfig`, and
  `zephyr/module.yml`.
