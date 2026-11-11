/*
 * Dongle split-link panel: per-half RSSI, drop count, last drop reason.
 *
 *   L -58  0      <- side, RSSI dBm (or "--" when down), drops since boot
 *   R -71  2
 *   R 08 12m      <- last drop: side, HCI reason (hex), how long ago
 *
 * Reasons worth knowing: 08 = supervision timeout (RF loss / half died),
 * 13 = half closed the link, 3e = link failed to establish, 22 = LL timeout.
 *
 * Sides are learned from the first key press each half sends (the central
 * only knows peripherals by address), so a half shows "?" until it is used.
 *
 * Threads: BT callbacks and the key listener only touch state under a
 * spinlock. RSSI is read via a synchronous HCI command on the low-priority
 * work queue (never the system queue — BT TX runs there). LVGL is only
 * touched on the display work queue.
 *
 * SPDX-License-Identifier: MIT
 */

#include "link_widget.h"

#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

#include <zmk/display.h>
#include <zmk/workqueue.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* Defined in zmk/app/src/split/bluetooth/central.c, no public header. */
int peripheral_slot_index_for_conn(struct bt_conn *conn);

#define MAX_LINKS 2
#define POLL_MS 2000

struct link {
    bool used;
    bt_addr_le_t addr;
    char side; /* 'L', 'R' or '?' */
    bool up;
    bool rssi_valid;
    int8_t rssi;
    uint16_t drops;
};

static struct k_spinlock lock;
static struct link links[MAX_LINKS];
static bool have_drop;
static char last_drop_side = '?';
static uint8_t last_drop_reason;
static int64_t last_drop_ms;

static lv_obj_t *label;
static char shown[48];

/* Keymap positions (sofled.dtsi transform): rows 0-2 are 6+6, row 3 is
 * 7+7 (thumb-row inner keys), row 4 is 5+5. */
static char side_for_position(uint32_t pos) {
    if (pos < 36) {
        return (pos % 12) < 6 ? 'L' : 'R';
    }
    if (pos < 50) {
        return pos < 43 ? 'L' : 'R';
    }
    return pos < 55 ? 'L' : 'R';
}

/* Caller holds lock. */
static struct link *link_for_addr(const bt_addr_le_t *addr, bool create) {
    for (int i = 0; i < MAX_LINKS; i++) {
        if (links[i].used && bt_addr_le_eq(&links[i].addr, addr)) {
            return &links[i];
        }
    }
    if (!create) {
        return NULL;
    }
    for (int i = 0; i < MAX_LINKS; i++) {
        if (!links[i].used) {
            links[i] = (struct link){.used = true, .side = '?'};
            bt_addr_le_copy(&links[i].addr, addr);
            return &links[i];
        }
    }
    return NULL;
}

static bool is_split_link(struct bt_conn *conn) {
    struct bt_conn_info info;
    return bt_conn_get_info(conn, &info) == 0 && info.role == BT_CONN_ROLE_CENTRAL;
}

/* ---------- display (display work queue only) ---------- */

static void fmt_age(char *buf, size_t len, int64_t ms) {
    int64_t s = ms / 1000;
    if (s < 60) {
        snprintf(buf, len, "%ds", (int)s);
    } else if (s < 3600) {
        snprintf(buf, len, "%dm", (int)(s / 60));
    } else if (s < 100 * 3600) {
        snprintf(buf, len, "%dh", (int)(s / 3600));
    } else {
        snprintf(buf, len, "%dd", (int)(s / 86400));
    }
}

static void render(struct k_work *work) {
    if (!label) {
        return;
    }

    char text[sizeof(shown)];
    size_t n = 0;

    k_spinlock_key_t key = k_spin_lock(&lock);
    struct link copy[MAX_LINKS];
    memcpy(copy, links, sizeof(copy));
    bool drop = have_drop;
    char dside = last_drop_side;
    uint8_t dreason = last_drop_reason;
    int64_t dms = last_drop_ms;
    k_spin_unlock(&lock, key);

    /* Left first, then right, then unknown — stable line order. */
    static const char order[] = {'L', 'R', '?'};
    int lines = 0;
    for (int o = 0; o < ARRAY_SIZE(order); o++) {
        for (int i = 0; i < MAX_LINKS; i++) {
            struct link *l = &copy[i];
            if (!l->used || l->side != order[o]) {
                continue;
            }
            char rssi[6];
            if (l->up && l->rssi_valid) {
                snprintf(rssi, sizeof(rssi), "%d", l->rssi);
            } else {
                strcpy(rssi, l->up ? ".." : "--");
            }
            n += snprintf(text + n, sizeof(text) - n, "%s%c %3s %2u", lines ? "\n" : "", l->side,
                          rssi, l->drops > 99 ? 99 : l->drops);
            lines++;
        }
    }
    if (lines == 0) {
        n += snprintf(text + n, sizeof(text) - n, "no halves");
        lines++;
    }
    if (drop) {
        char age[6];
        fmt_age(age, sizeof(age), k_uptime_get() - dms);
        snprintf(text + n, sizeof(text) - n, "\n%c %02x %s", dside, dreason, age);
    }

    if (strcmp(text, shown) != 0) {
        strcpy(shown, text);
        lv_label_set_text(label, text);
    }
}

static K_WORK_DEFINE(render_work, render);

static void request_render(void) { k_work_submit_to_queue(zmk_display_work_q(), &render_work); }

/* ---------- RSSI poll (low-priority work queue) ---------- */

struct conn_list {
    struct bt_conn *conns[MAX_LINKS];
    int count;
};

static void collect_conn(struct bt_conn *conn, void *data) {
    struct conn_list *list = data;
    if (list->count < MAX_LINKS && is_split_link(conn)) {
        list->conns[list->count++] = bt_conn_ref(conn);
    }
}

static int read_rssi(struct bt_conn *conn, int8_t *rssi) {
    uint16_t handle;
    int err = bt_hci_get_conn_handle(conn, &handle);
    if (err) {
        return err;
    }

    struct net_buf *buf = bt_hci_cmd_create(BT_HCI_OP_READ_RSSI, sizeof(struct bt_hci_cp_read_rssi));
    if (!buf) {
        return -ENOMEM;
    }
    struct bt_hci_cp_read_rssi *cp = net_buf_add(buf, sizeof(*cp));
    cp->handle = sys_cpu_to_le16(handle);

    struct net_buf *rsp = NULL;
    err = bt_hci_cmd_send_sync(BT_HCI_OP_READ_RSSI, buf, &rsp);
    if (err) {
        return err;
    }
    struct bt_hci_rp_read_rssi *rp = (void *)rsp->data;
    err = rp->status ? -EIO : 0;
    *rssi = rp->rssi;
    net_buf_unref(rsp);
    return err;
}

static void poll_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(poll_work, poll_handler);

static void poll_handler(struct k_work *work) {
    struct conn_list list = {0};
    bt_conn_foreach(BT_CONN_TYPE_LE, collect_conn, &list);

    for (int i = 0; i < list.count; i++) {
        int8_t rssi = 0;
        int err = read_rssi(list.conns[i], &rssi);

        k_spinlock_key_t key = k_spin_lock(&lock);
        struct link *l = link_for_addr(bt_conn_get_dst(list.conns[i]), false);
        if (l && l->up) {
            l->rssi_valid = (err == 0 && rssi != 127);
            l->rssi = rssi;
        }
        k_spin_unlock(&lock, key);

        bt_conn_unref(list.conns[i]);
    }

    request_render();
    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &poll_work, K_MSEC(POLL_MS));
}

/* ---------- connection events (BT context) ---------- */

static void on_connected(struct bt_conn *conn, uint8_t err) {
    if (err || !is_split_link(conn)) {
        return;
    }
    k_spinlock_key_t key = k_spin_lock(&lock);
    struct link *l = link_for_addr(bt_conn_get_dst(conn), true);
    if (l) {
        l->up = true;
        l->rssi_valid = false;
    }
    k_spin_unlock(&lock, key);
    request_render();
}

static void on_disconnected(struct bt_conn *conn, uint8_t reason) {
    if (!is_split_link(conn)) {
        return;
    }
    k_spinlock_key_t key = k_spin_lock(&lock);
    struct link *l = link_for_addr(bt_conn_get_dst(conn), false);
    if (l && l->up) {
        l->up = false;
        l->rssi_valid = false;
        l->drops++;
        have_drop = true;
        last_drop_side = l->side;
        last_drop_reason = reason;
        last_drop_ms = k_uptime_get();
    }
    char side = l ? l->side : '?';
    k_spin_unlock(&lock, key);
    LOG_WRN("split link %c dropped, reason 0x%02x", side, reason);
    request_render();
}

BT_CONN_CB_DEFINE(sofled_link_conn_cb) = {
    .connected = on_connected,
    .disconnected = on_disconnected,
};

/* ---------- side learning (key events) ---------- */

struct slot_match {
    int slot;
    struct bt_conn *conn;
};

static void match_slot(struct bt_conn *conn, void *data) {
    struct slot_match *m = data;
    if (!m->conn && is_split_link(conn) && peripheral_slot_index_for_conn(conn) == m->slot) {
        m->conn = bt_conn_ref(conn);
    }
}

static int link_position_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev || !ev->state || ev->source == ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    /* Cheap exit once both halves are identified. */
    bool all_known = true;
    k_spinlock_key_t key = k_spin_lock(&lock);
    for (int i = 0; i < MAX_LINKS; i++) {
        if (!links[i].used || links[i].side == '?') {
            all_known = false;
        }
    }
    k_spin_unlock(&lock, key);
    if (all_known) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    struct slot_match m = {.slot = ev->source};
    bt_conn_foreach(BT_CONN_TYPE_LE, match_slot, &m);
    if (!m.conn) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    char side = side_for_position(ev->position);
    key = k_spin_lock(&lock);
    struct link *l = link_for_addr(bt_conn_get_dst(m.conn), true);
    bool changed = l && l->side != side;
    if (changed) {
        l->side = side;
    }
    k_spin_unlock(&lock, key);
    bt_conn_unref(m.conn);

    if (changed) {
        request_render();
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(sofled_link_widget, link_position_listener);
ZMK_SUBSCRIPTION(sofled_link_widget, zmk_position_state_changed);

/* ---------- init ---------- */

lv_obj_t *link_widget_init(lv_obj_t *parent) {
    label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, &lv_font_unscii_8, LV_PART_MAIN);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_label_set_text(label, "");

    k_work_reschedule_for_queue(zmk_workqueue_lowprio_work_q(), &poll_work, K_MSEC(POLL_MS));
    return label;
}
