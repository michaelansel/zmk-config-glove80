/*
 * Dongle display for Glove80 three-device split.
 * Hardware: Seeed XIAO nRF52840 + Waveshare 1.69" round LCD (ST7789V, 240×280, RGB565)
 *
 * Layout (round glass ~240px diameter, centred on the display):
 *
 *           ╭───────────────────╮
 *           │   [USB / BT n ✓]  │  ← connection, top-centre
 *           │                   │
 *           │    LAYER  NAME    │  ← layer name, large, centre
 *           │      #layer_n     │  ← layer number, small, dimmed
 *           │                   │
 *           │   L 85%   72% R   │  ← peripheral battery, bottom
 *           ╰───────────────────╯
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/keymap.h>
#include <zmk/endpoints.h>
#include <zmk/ble.h>
#include <zmk/split/central.h>

#include <lvgl.h>

/* ── colours ─────────────────────────────────────────────────────── */
#define CLR_BG       0x0d1117
#define CLR_FG       0xe6edf3
#define CLR_DIM      0x6e7681
#define CLR_GREEN    0x3fb950
#define CLR_AMBER    0xd29922
#define CLR_RED      0xf85149
#define CLR_BLUE     0x388bfd

/* ── UI state (written from event thread, read from display work-q) ─ */
static struct {
    zmk_keymap_layer_index_t layer_index;
    const char              *layer_name;
    struct zmk_endpoint_instance endpoint;
    bool                     ble_connected;
    bool                     ble_bonded;
    uint8_t                  bat[2];      /* peripheral 0=left 1=right */
    bool                     bat_known[2];
} g;

/* ── LVGL object handles ─────────────────────────────────────────── */
static lv_obj_t *conn_label;
static lv_obj_t *layer_name_label;
static lv_obj_t *layer_num_label;
static lv_obj_t *bat_label[2];

/* ── helpers ─────────────────────────────────────────────────────── */
static lv_color_t bat_color(uint8_t pct) {
    if (pct > 60) return lv_color_hex(CLR_GREEN);
    if (pct > 20) return lv_color_hex(CLR_AMBER);
    return lv_color_hex(CLR_RED);
}

/* ── render: called on the dedicated display work queue ───────────── */
static void do_render(struct k_work *work) {
    /* connection label */
    char conn_buf[24];
    if (g.endpoint.transport == ZMK_TRANSPORT_USB) {
        lv_obj_set_style_text_color(conn_label, lv_color_hex(CLR_GREEN), 0);
        snprintf(conn_buf, sizeof(conn_buf), LV_SYMBOL_USB " USB");
    } else {
        int profile = g.endpoint.ble.profile_index + 1;
        if (g.ble_connected) {
            lv_obj_set_style_text_color(conn_label, lv_color_hex(CLR_BLUE), 0);
            snprintf(conn_buf, sizeof(conn_buf),
                     LV_SYMBOL_WIFI " BT %d " LV_SYMBOL_OK, profile);
        } else if (g.ble_bonded) {
            lv_obj_set_style_text_color(conn_label, lv_color_hex(CLR_AMBER), 0);
            snprintf(conn_buf, sizeof(conn_buf),
                     LV_SYMBOL_WIFI " BT %d " LV_SYMBOL_CLOSE, profile);
        } else {
            lv_obj_set_style_text_color(conn_label, lv_color_hex(CLR_DIM), 0);
            snprintf(conn_buf, sizeof(conn_buf),
                     LV_SYMBOL_WIFI " BT %d ?", profile);
        }
    }
    lv_label_set_text(conn_label, conn_buf);

    /* layer name */
    if (g.layer_name && g.layer_name[0]) {
        lv_label_set_text(layer_name_label, g.layer_name);
    } else {
        char tmp[8];
        snprintf(tmp, sizeof(tmp), "#%u", g.layer_index);
        lv_label_set_text(layer_name_label, tmp);
    }

    /* layer number */
    char num_buf[8];
    snprintf(num_buf, sizeof(num_buf), "#%u", g.layer_index);
    lv_label_set_text(layer_num_label, num_buf);

    /* battery labels: "L 85%" / "R  ?" */
    static const char *const sides[2] = {"L", "R"};
    for (int i = 0; i < 2; i++) {
        char buf[12];
        if (g.bat_known[i]) {
            lv_obj_set_style_text_color(bat_label[i], bat_color(g.bat[i]), 0);
            snprintf(buf, sizeof(buf), "%s %u%%", sides[i], g.bat[i]);
        } else {
            lv_obj_set_style_text_color(bat_label[i], lv_color_hex(CLR_DIM), 0);
            snprintf(buf, sizeof(buf), "%s  ?", sides[i]);
        }
        lv_label_set_text(bat_label[i], buf);
    }
}

K_WORK_DEFINE(render_work, do_render);

static void schedule_render(void) {
    k_work_submit_to_queue(zmk_display_work_q(), &render_work);
}

/* ── event handlers ──────────────────────────────────────────────── */
static int on_layer_changed(const zmk_event_t *eh) {
    zmk_keymap_layer_index_t idx = zmk_keymap_highest_layer_active();
    g.layer_index = idx;
    g.layer_name  = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(idx));
    schedule_render();
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(dongle_layer, on_layer_changed);
ZMK_SUBSCRIPTION(dongle_layer, zmk_layer_state_changed);

static int on_endpoint_changed(const zmk_event_t *eh) {
    g.endpoint      = zmk_endpoints_get_selected();
    g.ble_connected = zmk_ble_active_profile_is_connected();
    g.ble_bonded    = !zmk_ble_active_profile_is_open();
    schedule_render();
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(dongle_endpoint, on_endpoint_changed);
ZMK_SUBSCRIPTION(dongle_endpoint, zmk_endpoint_changed);
ZMK_SUBSCRIPTION(dongle_endpoint, zmk_ble_active_profile_changed);

static int on_peripheral_battery(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (!ev || ev->source >= 2) return ZMK_EV_EVENT_BUBBLE;
    g.bat[ev->source]       = ev->state_of_charge;
    g.bat_known[ev->source] = true;
    schedule_render();
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(dongle_battery, on_peripheral_battery);
ZMK_SUBSCRIPTION(dongle_battery, zmk_peripheral_battery_state_changed);

/* ── screen construction ─────────────────────────────────────────── */
lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_size(screen, 240, 280);
    lv_obj_set_style_bg_color(screen, lv_color_hex(CLR_BG), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

    /* ── connection label (top centre, y≈50) ── */
    conn_label = lv_label_create(screen);
    lv_obj_set_style_text_font(conn_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(conn_label, lv_color_hex(CLR_DIM), 0);
    lv_label_set_text(conn_label, LV_SYMBOL_WIFI " ...");
    lv_obj_align(conn_label, LV_ALIGN_TOP_MID, 0, 46);

    /* ── thin separator line below connection ── */
    lv_obj_t *sep1 = lv_obj_create(screen);
    lv_obj_set_size(sep1, 120, 1);
    lv_obj_set_style_bg_color(sep1, lv_color_hex(CLR_DIM), 0);
    lv_obj_set_style_bg_opa(sep1, LV_OPA_40, 0);
    lv_obj_set_style_border_width(sep1, 0, 0);
    lv_obj_set_style_radius(sep1, 0, 0);
    lv_obj_align(sep1, LV_ALIGN_TOP_MID, 0, 72);

    /* ── layer name (centre, large) ── */
    layer_name_label = lv_label_create(screen);
    lv_obj_set_style_text_font(layer_name_label, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(layer_name_label, lv_color_hex(CLR_FG), 0);
    lv_obj_set_style_text_align(layer_name_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(layer_name_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(layer_name_label, 200);
    lv_label_set_text(layer_name_label, "...");
    lv_obj_align(layer_name_label, LV_ALIGN_CENTER, 0, -18);

    /* ── layer number (below name, small dimmed) ── */
    layer_num_label = lv_label_create(screen);
    lv_obj_set_style_text_font(layer_num_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(layer_num_label, lv_color_hex(CLR_DIM), 0);
    lv_label_set_text(layer_num_label, "#0");
    lv_obj_align(layer_num_label, LV_ALIGN_CENTER, 0, 32);

    /* ── thin separator above battery row ── */
    lv_obj_t *sep2 = lv_obj_create(screen);
    lv_obj_set_size(sep2, 120, 1);
    lv_obj_set_style_bg_color(sep2, lv_color_hex(CLR_DIM), 0);
    lv_obj_set_style_bg_opa(sep2, LV_OPA_40, 0);
    lv_obj_set_style_border_width(sep2, 0, 0);
    lv_obj_set_style_radius(sep2, 0, 0);
    lv_obj_align(sep2, LV_ALIGN_BOTTOM_MID, 0, -68);

    /* ── battery labels: L on left, R on right ── */
    bat_label[0] = lv_label_create(screen);
    lv_obj_set_style_text_font(bat_label[0], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bat_label[0], lv_color_hex(CLR_DIM), 0);
    lv_label_set_text(bat_label[0], "L  ?");
    lv_obj_align(bat_label[0], LV_ALIGN_BOTTOM_LEFT, 30, -40);

    bat_label[1] = lv_label_create(screen);
    lv_obj_set_style_text_font(bat_label[1], &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(bat_label[1], lv_color_hex(CLR_DIM), 0);
    lv_label_set_text(bat_label[1], "R  ?");
    lv_obj_align(bat_label[1], LV_ALIGN_BOTTOM_RIGHT, -30, -40);

    /* seed initial state */
    zmk_keymap_layer_index_t idx = zmk_keymap_highest_layer_active();
    g.layer_index = idx;
    g.layer_name  = zmk_keymap_layer_name(zmk_keymap_layer_index_to_id(idx));
    g.endpoint      = zmk_endpoints_get_selected();
    g.ble_connected = zmk_ble_active_profile_is_connected();
    g.ble_bonded    = !zmk_ble_active_profile_is_open();

#if IS_ENABLED(CONFIG_ZMK_SPLIT_BLE_CENTRAL_BATTERY_LEVEL_FETCHING)
    for (int i = 0; i < 2; i++) {
        uint8_t lvl = 0;
        if (zmk_split_central_get_peripheral_battery_level(i, &lvl) == 0) {
            g.bat[i]       = lvl;
            g.bat_known[i] = true;
        }
    }
#endif

    schedule_render();
    return screen;
}
