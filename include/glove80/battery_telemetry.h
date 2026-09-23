/*
 * battery_telemetry — shared cache of split peripheral battery state on the
 * dongle, plus the periodic G80BAT heartbeat line (docs/battery-telemetry.md).
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Current battery reading for a peripheral (0 = left, 1 = right — really the
 * split slot index, which follows pairing order).
 *
 * Returns false if the side is not currently reporting (never seen, or
 * disconnected). Otherwise fills *pct and *age_ms, the ms since the
 * percentage last changed. Either out-pointer may be NULL.
 */
bool g80_battery_get(uint8_t side, uint8_t *pct, int64_t *age_ms);

/* Number of split peripherals with a live BLE connection to the dongle. */
int g80_split_connected(void);
