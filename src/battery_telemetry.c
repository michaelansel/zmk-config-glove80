/*
 * battery_telemetry — owns the dongle's cache of peripheral battery state and
 * prints it as a G80BAT line on the USB serial log every
 * CONFIG_GLOVE80_BATTERY_HEARTBEAT_MS. Format and semantics are a contract
 * with the host reader: see docs/battery-telemetry.md before changing them.
 *
 *   G80BAT v=1 up=109662 fw=20260923T1412-591f01d l=85 la=3421 r=72 ra=51 split=2
 *
 * Reading is free: the line is built from state the central already holds.
 * Nothing here causes the halves to do any work.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/conn.h>
#include <stdio.h>

#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>

#include <glove80/battery_telemetry.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define G80BAT_VERSION 1
#define NUM_SIDES      2

#ifndef G80_FW_ID
#define G80_FW_ID "unknown"
#endif

/* ---------- Cache ---------- */

struct side_state {
    uint8_t pct;
    bool    have_value;  /* a reading has ever arrived since boot */
    bool    live;        /* currently connected and reporting */
    int64_t changed_at;  /* uptime ms when pct last took a new value */
};

static struct side_state sides[NUM_SIDES];

/*
 * The central raises zmk_peripheral_battery_state_changed for each source:
 *   - non-zero level: a notification, or the read done on (re)connect
 *   - zero level: central.c fires this on disconnect to clear the reading
 * The "changed" stamp moves only when the percentage differs from the cached
 * one, so a reconnect that reports the same value keeps its age. On
 * disconnect the value and stamp are kept; the side just stops being live.
 */
static int on_peripheral_battery(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (!ev || ev->source >= NUM_SIDES) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    struct side_state *s = &sides[ev->source];
    if (ev->state_of_charge > 0) {
        s->live = true;
        if (!s->have_value || s->pct != ev->state_of_charge) {
            s->pct        = ev->state_of_charge;
            s->have_value = true;
            s->changed_at = k_uptime_get();
        }
        LOG_INF("peripheral %d battery: %d%%", ev->source, ev->state_of_charge);
    } else {
        s->live = false;
        LOG_INF("peripheral %d battery: 0 (disconnect)", ev->source);
    }
    return ZMK_EV_EVENT_BUBBLE;
}
ZMK_LISTENER(g80_battery, on_peripheral_battery);
ZMK_SUBSCRIPTION(g80_battery, zmk_peripheral_battery_state_changed);

bool g80_battery_get(uint8_t side, uint8_t *pct, int64_t *age_ms) {
    if (side >= NUM_SIDES || !sides[side].live) {
        return false;
    }
    if (pct) {
        *pct = sides[side].pct;
    }
    if (age_ms) {
        *age_ms = k_uptime_get() - sides[side].changed_at;
    }
    return true;
}

/* The dongle is BLE central only toward the split halves; host links (if any)
 * have the peripheral role, so counting central-role links counts halves. */
static void count_split_conn(struct bt_conn *conn, void *data) {
    struct bt_conn_info info;
    if (bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_CENTRAL &&
        info.state == BT_CONN_STATE_CONNECTED) {
        (*(int *)data)++;
    }
}

int g80_split_connected(void) {
    int n = 0;
    bt_conn_foreach(BT_CONN_TYPE_LE, count_split_conn, &n);
    return n;
}

/* ---------- Heartbeat ---------- */

struct side_fields {
    char pct[4]; /* "100" or "na" */
    char age[21];
};

static void fmt_side(uint8_t side, struct side_fields *f) {
    uint8_t pct;
    int64_t age;
    if (g80_battery_get(side, &pct, &age)) {
        snprintf(f->pct, sizeof(f->pct), "%d", (int)pct);
        snprintf(f->age, sizeof(f->age), "%lld", (long long)age);
    } else {
        snprintf(f->pct, sizeof(f->pct), "na");
        snprintf(f->age, sizeof(f->age), "na");
    }
}

static struct k_work_delayable heartbeat_work;

static void heartbeat_fn(struct k_work *work) {
    struct side_fields l, r;
    char line[192];

    fmt_side(0, &l);
    fmt_side(1, &r);

    snprintf(line, sizeof(line), "G80BAT v=%d up=%lld fw=%s l=%s la=%s r=%s ra=%s split=%d",
             G80BAT_VERSION, (long long)k_uptime_get(), G80_FW_ID, l.pct, l.age, r.pct, r.age,
             g80_split_connected());

    /* printk is formatted in place and logged unconditionally (LOG_PRINTK), with
     * no "<inf> zmk:" prefix, so the line survives any ZMK log level. */
    printk("%s\n", line);

    k_work_reschedule(&heartbeat_work, K_MSEC(CONFIG_GLOVE80_BATTERY_HEARTBEAT_MS));
}

static int battery_telemetry_init(void) {
    k_work_init_delayable(&heartbeat_work, heartbeat_fn);
    k_work_reschedule(&heartbeat_work, K_MSEC(CONFIG_GLOVE80_BATTERY_HEARTBEAT_MS));
    return 0;
}

SYS_INIT(battery_telemetry_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
