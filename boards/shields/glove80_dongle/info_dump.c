/*
 * info_dump — ZMK behavior that types keyboard status into the host on keypress.
 *
 * Bind &info_dump on the MAGIC layer; tap the key while holding MAGIC, then
 * release MAGIC and position cursor in any text field — status types itself
 * after a short delay.
 *
 * The full status (including build timestamp) is also logged to USB serial.
 *
 * Output format:
 *   === glove80 zmk ===
 *   endpoint = usb
 *   bat = l=85 r=90
 *   layer = 3 [base_layer]
 *   ===================
 */

#define DT_DRV_COMPAT zmk_behavior_info_dump

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/endpoints.h>
#include <zmk/ble.h>

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
#include <zmk/split/central.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ---------- ZMK keycode encoding ----------
 *
 * raise_zmk_keycode_state_changed_from_encoded() decodes a 32-bit value as:
 *   bits 31..24  usage page  (0 = implicit HID_USAGE_KEY = 0x07)
 *   bits 23..16  implicit modifiers (ZMK mod byte: LSHIFT = bit 1 = 0x02)
 *   bits 15..0   HID usage ID
 */

#define _KC(id)         ((uint32_t)(id))            /* keyboard key, no modifier */
#define _KC_SHF(id)     ((uint32_t)(0x02) << 16 | (id))  /* keyboard key + left shift */

/* HID keyboard usage IDs (USB HID spec, usage page 0x07) */
static const uint32_t kc_alpha_lower[26] = {
    /* a */ _KC(0x04), _KC(0x05), _KC(0x06), _KC(0x07), _KC(0x08), _KC(0x09),
    /* g */ _KC(0x0A), _KC(0x0B), _KC(0x0C), _KC(0x0D), _KC(0x0E), _KC(0x0F),
    /* m */ _KC(0x10), _KC(0x11), _KC(0x12), _KC(0x13), _KC(0x14), _KC(0x15),
    /* s */ _KC(0x16), _KC(0x17), _KC(0x18), _KC(0x19), _KC(0x1A), _KC(0x1B),
    /* y */ _KC(0x1C), _KC(0x1D),
};

static uint32_t char_to_keycode(char c)
{
    /* Lowercase letters */
    if (c >= 'a' && c <= 'z') return kc_alpha_lower[c - 'a'];
    /* Digits 1-9, then 0 */
    if (c >= '1' && c <= '9') return _KC(0x1E + (c - '1'));
    if (c == '0')              return _KC(0x27);

    switch (c) {
        case '\n': return _KC(0x28);   /* Enter */
        case ' ':  return _KC(0x2C);   /* Space */
        case '-':  return _KC(0x2D);   /* Minus / hyphen */
        case '=':  return _KC(0x2E);   /* Equals */
        case '[':  return _KC(0x2F);   /* [ */
        case ']':  return _KC(0x30);   /* ] */
        case ';':  return _KC(0x33);   /* Semicolon */
        case '.':  return _KC(0x37);   /* Period */
        case '/':  return _KC(0x38);   /* Forward slash */
        default:   return 0;           /* Unmapped — skipped */
    }
}

/* ---------- Async typing state machine ---------- */

static char   type_buf[512];
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
        /* Skip unmapped character, reschedule immediately */
        k_work_reschedule(&type_work, K_MSEC(1));
        return;
    }

    int64_t ts = k_uptime_get();
    raise_zmk_keycode_state_changed_from_encoded(kc, true,  ts);
    raise_zmk_keycode_state_changed_from_encoded(kc, false, ts);

    k_work_reschedule(&type_work, K_MSEC(40));
}

/* ---------- Status string builder ----------
 *
 * Uses only lowercase letters, digits, and unshifted symbols so that
 * char_to_keycode() covers every character in the output.
 */

static void lowercase_copy(char *dst, const char *src, size_t max)
{
    size_t i;
    for (i = 0; i + 1 < max && src[i]; i++) {
        dst[i] = (char)tolower((unsigned char)src[i]);
    }
    dst[i] = '\0';
}

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

    struct zmk_endpoint_instance ep = zmk_endpoint_get_selected();
    if (ep.transport == ZMK_TRANSPORT_USB) {
        AP("endpoint = usb\n");
    } else {
        AP("endpoint = ble profile %d", ep.ble.profile_index);
        if (zmk_ble_active_profile_is_connected()) {
            AP(" [connected]\n");
        } else {
            AP(" [disconnected]\n");
        }
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    {
        uint8_t lbat = 0, rbat = 0;
        bool lknown = (zmk_split_central_get_peripheral_battery_level(0, &lbat) == 0);
        bool rknown = (zmk_split_central_get_peripheral_battery_level(1, &rbat) == 0);
        AP("bat = l=");
        if (lknown) { AP("%d", (int)lbat); } else { AP("?"); }
        AP(" r=");
        if (rknown) { AP("%d", (int)rbat); } else { AP("?"); }
        AP("\n");
    }
#endif

    {
        zmk_keymap_layer_index_t layer = zmk_keymap_highest_layer_active();
        const char *name = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(layer));
        char lname[32] = "";
        if (name && name[0]) {
            lowercase_copy(lname, name, sizeof(lname));
            AP("layer = %d [%s]\n", (int)layer, lname);
        } else {
            AP("layer = %d\n", (int)layer);
        }
    }

    AP("===================\n");

#undef AP

    type_len = (size_t)n;
    type_pos = 0;

    /* Also log to USB serial (which can handle uppercase/colons freely) */
    LOG_INF("info_dump built %s %s: %.*s", __DATE__, __TIME__, (int)type_len, buf);
}

/* ---------- ZMK behavior callbacks ---------- */

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event)
{
    k_work_cancel_delayable(&type_work);
    build_status();
    /* Delay before typing so the user can release MAGIC and position cursor */
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
