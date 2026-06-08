/*
 * info_dump — ZMK behavior that types keyboard status into the host on keypress.
 *
 * Bind &info_dump on the MAGIC layer; tap the key while holding MAGIC, then
 * release MAGIC and position cursor in any text field — status types itself
 * after a 500 ms delay.
 *
 * The full status (including build timestamp) is also logged to USB serial.
 *
 * Typed output (all lowercase / unshifted chars only):
 *   === glove80 zmk ===
 *   uptime = 12345 s
 *   output = usb
 *   bt0 = open [active]
 *   bt1 = bonded aa-bb-cc-dd-ee-ff
 *   bt2 = connected dd-ee-ff-00-11-22
 *   bt3 = open
 *   split = 2/2
 *   bat = l=85 r=72
 *   ===================
 */

#define DT_DRV_COMPAT zmk_behavior_info_dump

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/endpoints.h>
#include <zmk/ble.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
#include <zmk/split/central.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ---------- Persistent state updated by event subscriptions ---------- */

#define NUM_BLE_PROFILES 4  /* matches bt_0…bt_3 macros in keymap */

static uint8_t bat_val[2];
static bool    bat_known[2];
static bool    split_connected[2];

/*
 * The central raises zmk_peripheral_battery_state_changed for each source:
 *   - non-zero level: peripheral is connected and reporting battery
 *   - zero level: central.c fires this on disconnect to clear the reading
 * We use this as the sole signal for both battery value and peripheral presence.
 * (zmk_split_peripheral_status_changed is only raised on the peripheral side,
 * never on the central/dongle, so we cannot use it here.)
 */
static int on_peripheral_battery(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (ev && ev->source < 2) {
        if (ev->state_of_charge > 0) {
            bat_val[ev->source]       = ev->state_of_charge;
            bat_known[ev->source]     = true;
            split_connected[ev->source] = true;
        } else {
            bat_known[ev->source]     = false;
            split_connected[ev->source] = false;
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(info_dump_bat, on_peripheral_battery);
ZMK_SUBSCRIPTION(info_dump_bat, zmk_peripheral_battery_state_changed);

/* ---------- ASCII → ZMK encoded keycode ----------
 *
 * raise_zmk_keycode_state_changed_from_encoded() format:
 *   bits 23..16  implicit modifiers  (LSHIFT = 0x02)
 *   bits 15..0   HID usage ID (keyboard page implicit)
 *
 * We keep to lowercase letters, digits, and unshifted punctuation so that
 * every character in the typed output maps cleanly.
 */

#define _KC(id)     ((uint32_t)(id))

static uint32_t char_to_keycode(char c)
{
    if (c >= 'a' && c <= 'z') return _KC(0x04 + (c - 'a'));
    if (c >= '1' && c <= '9') return _KC(0x1E + (c - '1'));
    if (c == '0')              return _KC(0x27);
    switch (c) {
        case '\n': return _KC(0x28);
        case ' ':  return _KC(0x2C);
        case '-':  return _KC(0x2D);
        case '=':  return _KC(0x2E);
        case '[':  return _KC(0x2F);
        case ']':  return _KC(0x30);
        case '/':  return _KC(0x38);
        default:   return 0;
    }
}

/* ---------- Async typing state machine ---------- */

static char   type_buf[768];
static size_t type_len;
static size_t type_pos;

static struct k_work_delayable type_work;

static void type_work_fn(struct k_work *work)
{
    if (type_pos >= type_len) {
        return;
    }

    char     c  = type_buf[type_pos++];
    uint32_t kc = char_to_keycode(c);

    if (kc == 0) {
        k_work_reschedule(&type_work, K_MSEC(1));
        return;
    }

    int64_t ts = k_uptime_get();
    raise_zmk_keycode_state_changed_from_encoded(kc, true,  ts);
    raise_zmk_keycode_state_changed_from_encoded(kc, false, ts);

    k_work_reschedule(&type_work, K_MSEC(40));
}

/* ---------- Status string builder ---------- */

static void build_status(void)
{
    int   n   = 0;
    int   sz  = (int)sizeof(type_buf) - 1;
    char *buf = type_buf;

#define AP(...) do { \
    int _r = snprintf(buf + n, sz - n, __VA_ARGS__); \
    if (_r > 0) n += MIN(_r, sz - n); \
} while (0)

    AP("=== glove80 zmk ===\n");

    /* Uptime */
    AP("uptime = %lld s\n", (long long)(k_uptime_get() / 1000));

    /* Current output transport */
    struct zmk_endpoint_instance ep = zmk_endpoint_get_selected();
    if (ep.transport == ZMK_TRANSPORT_USB) {
        AP("output = usb\n");
    } else {
        AP("output = ble %d\n", ep.ble.profile_index);
    }

    /* All BLE host profiles */
    int active = zmk_ble_active_profile_index();
    for (int i = 0; i < NUM_BLE_PROFILES; i++) {
        AP("bt%d = ", i);
        if (zmk_ble_profile_is_open(i)) {
            AP("open");
        } else {
            /* bonded or connected — show partial address (last 3 bytes, LSB first in val[]) */
            bt_addr_le_t *addr = zmk_ble_profile_address(i);
            /* val[] is little-endian, so val[5] is MSB (first byte in standard notation) */
            AP("%s %02x-%02x-%02x-%02x-%02x-%02x",
               zmk_ble_profile_is_connected(i) ? "connected" : "bonded",
               addr->a.val[5], addr->a.val[4], addr->a.val[3],
               addr->a.val[2], addr->a.val[1], addr->a.val[0]);
        }
        if (i == active) {
            AP(" [active]");
        }
        AP("\n");
    }

    /* Split peripheral connection (inferred from battery events) */
    AP("split = %d/2\n", (int)split_connected[0] + (int)split_connected[1]);

    /* Peripheral battery — only show value if a notification has arrived */
    AP("bat = l=");
    if (bat_known[0]) { AP("%d", (int)bat_val[0]); } else { AP("?"); }
    AP(" r=");
    if (bat_known[1]) { AP("%d", (int)bat_val[1]); } else { AP("?"); }
    AP("\n");

    AP("===================\n");

#undef AP

    type_len = (size_t)n;
    type_pos = 0;

    LOG_INF("info_dump built=%s %s:\n%.*s", __DATE__, __TIME__, (int)type_len, buf);
}

/* ---------- ZMK behavior callbacks ---------- */

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event)
{
    k_work_cancel_delayable(&type_work);
    build_status();
    k_work_reschedule(&type_work, K_MSEC(500));
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event)
{
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api info_dump_driver_api = {
    .binding_pressed  = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

static int info_dump_init(const struct device *dev)
{
    k_work_init_delayable(&type_work, type_work_fn);
    return 0;
}

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

BEHAVIOR_DT_INST_DEFINE(0, info_dump_init, NULL, NULL, NULL,
                        POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &info_dump_driver_api);

#endif /* DT_HAS_COMPAT_STATUS_OKAY */
