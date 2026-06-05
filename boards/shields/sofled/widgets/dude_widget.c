/*
 * Claude critter "dude" widget implementation.
 *
 * Each keypress on this half ticks a counter. A sliding window over recent
 * presses yields a local WPM estimate; the state machine maps that (plus
 * idle time + battery) to a sprite frame. A periodic work item drives
 * animation transitions and the working/cooking two-frame walk cycle.
 *
 * SPDX-License-Identifier: MIT
 */

#include "dude_widget.h"
#include "dude_sprites.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/battery.h>
#include <zmk/split/bluetooth/peripheral.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ----- activity tracking ----- */

#define KEY_RING_SIZE 32
#define WPM_WINDOW_MS 5000   /* count keys in last 5s */
#define WPM_MULTIPLIER 12    /* keys/5s -> ~ keys/min / 5 chars-per-word */

static int64_t key_times_ms[KEY_RING_SIZE];
static uint8_t key_ring_head;
static int64_t last_key_ms;

/* burst detection — mash if >=5 keys within 200ms */
#define BURST_KEYS 5
#define BURST_WINDOW_MS 200
#define MASH_HOLD_MS 600

static int64_t mash_until_ms;

/* battery — droopy eyes if low */
#define LOW_BATT_PCT 20
static uint8_t batt_pct = 100;

/* idle threshold for sleep */
#define SLEEP_AFTER_MS 30000

/* ----- state ----- */

enum dude_state {
    DS_IDLE,
    DS_WORKING,
    DS_COOKING,
    DS_HYPER,
    DS_SLEEP,
    DS_MASH,
    DS_DROOP,
};

static enum dude_state cur_state = DS_IDLE;
static uint8_t walk_phase;     /* toggles 0/1 for two-frame animations */
static bool blink_now;         /* idle-blink one-shot */
static int64_t next_blink_ms;

/* widget singleton (only one display per shield) */
static struct dude_widget *the_widget;

static const lv_image_dsc_t *frame_for_state(enum dude_state s, uint8_t phase) {
    switch (s) {
    case DS_IDLE:    return blink_now ? &dude_blink : &dude_idle;
    case DS_WORKING: return phase ? &dude_working_b : &dude_working_a;
    case DS_COOKING: return phase ? &dude_cooking_b : &dude_cooking_a;
    case DS_HYPER:   return &dude_hyper;
    case DS_SLEEP:   return &dude_sleep;
    case DS_MASH:    return &dude_mash;
    case DS_DROOP:   return &dude_droop;
    }
    return &dude_idle;
}

static const char *label_for_state(enum dude_state s) {
    switch (s) {
    case DS_IDLE:    return "idle";
    case DS_WORKING: return "working";
    case DS_COOKING: return "cooking";
    case DS_HYPER:   return "hyper!!";
    case DS_SLEEP:   return "zzz";
    case DS_MASH:    return ">_<";
    case DS_DROOP:   return "low batt";
    }
    return "";
}

static uint8_t window_wpm_estimate(int64_t now_ms) {
    int n = 0;
    for (int i = 0; i < KEY_RING_SIZE; i++) {
        int64_t t = key_times_ms[i];
        if (t > 0 && (now_ms - t) <= WPM_WINDOW_MS) {
            n++;
        }
    }
    /* n keys in 5s -> approx wpm */
    return (uint8_t)(n * WPM_MULTIPLIER);
}

static bool burst_detected(int64_t now_ms) {
    int n = 0;
    for (int i = 0; i < KEY_RING_SIZE; i++) {
        int64_t t = key_times_ms[i];
        if (t > 0 && (now_ms - t) <= BURST_WINDOW_MS) {
            n++;
        }
    }
    return n >= BURST_KEYS;
}

static enum dude_state classify(int64_t now_ms) {
    if (now_ms < mash_until_ms) {
        return DS_MASH;
    }
    if (batt_pct > 0 && batt_pct < LOW_BATT_PCT && last_key_ms == 0) {
        return DS_DROOP;
    }
    if (last_key_ms == 0 || (now_ms - last_key_ms) > SLEEP_AFTER_MS) {
        return DS_SLEEP;
    }
    uint8_t wpm = window_wpm_estimate(now_ms);
    if (wpm == 0)       return DS_IDLE;
    if (wpm < 40)       return DS_WORKING;
    if (wpm < 80)       return DS_COOKING;
    return DS_HYPER;
}

/* ----- LVGL update ----- */

static const lv_image_dsc_t *last_frame;

static void apply_frame(struct dude_widget *w, enum dude_state s) {
    if (!w || !w->sprite) return;
    const lv_image_dsc_t *frame = frame_for_state(s, walk_phase);
    /* Only call lv_image_set_src when the frame actually changes.
     * Otherwise we trigger 8 redraws/sec of identical content, which the
     * Sharp Memory LCD shows as visible flicker. */
    if (frame != last_frame) {
        lv_image_set_src(w->sprite, frame);
        last_frame = frame;
    }
    if (w->caption) {
        lv_label_set_text(w->caption, label_for_state(s));
    }
    if (w->batt_label) {
        lv_label_set_text_fmt(w->batt_label, "%u%%", batt_pct);
    }
    if (w->conn_label) {
        bool conn = zmk_split_bt_peripheral_is_connected();
        /* When connected, show WIFI + tick. When disconnected, just the
         * WIFI symbol — drops the trailing glyph entirely so the wifi
         * icon's position stays fixed regardless of state. */
        lv_label_set_text(w->conn_label,
                          conn ? LV_SYMBOL_WIFI " " LV_SYMBOL_OK
                               : LV_SYMBOL_WIFI);
    }
}

/* ----- periodic tick ----- */

static void dude_tick_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(dude_tick_work, dude_tick_handler);

static void schedule_tick(void) {
    /* 120ms — snappy state changes; redraws are still skipped when the
     * actual frame doesn't change so idle is quiet. */
    k_work_schedule(&dude_tick_work, K_MSEC(120));
}

static void dude_tick_handler(struct k_work *work) {
    int64_t now = k_uptime_get();

    /* one-shot blink during idle */
    if (next_blink_ms == 0) {
        next_blink_ms = now + 3000 + (((uint32_t)now * 1103515245u + 12345u) % 3000u);
    }
    if (cur_state == DS_IDLE && now >= next_blink_ms) {
        blink_now = !blink_now;
        if (!blink_now) {
            next_blink_ms = now + 3000 + (((uint32_t)now * 1103515245u + 12345u) % 3000u);
        }
    } else {
        blink_now = false;
    }

    walk_phase ^= 1;
    enum dude_state s = classify(now);
    cur_state = s;
    apply_frame(the_widget, s);
    schedule_tick();
}

/* ----- event subscriptions ----- */

static int dude_key_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev || !ev->state) {
        return 0;
    }
    int64_t now = k_uptime_get();
    key_times_ms[key_ring_head] = now;
    key_ring_head = (key_ring_head + 1) % KEY_RING_SIZE;
    last_key_ms = now;

    if (burst_detected(now)) {
        mash_until_ms = now + MASH_HOLD_MS;
    }
    return 0;
}
ZMK_LISTENER(dude_key, dude_key_listener);
ZMK_SUBSCRIPTION(dude_key, zmk_position_state_changed);

static int dude_conn_listener(const zmk_event_t *eh) {
    /* Force a redraw next tick by clearing last_frame so apply_frame
     * re-emits even if the dude state hasn't changed. */
    last_frame = NULL;
    return 0;
}
ZMK_LISTENER(dude_conn, dude_conn_listener);
ZMK_SUBSCRIPTION(dude_conn, zmk_split_peripheral_status_changed);

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
static int dude_batt_listener(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    if (ev) {
        batt_pct = ev->state_of_charge;
    }
    return 0;
}
ZMK_LISTENER(dude_batt, dude_batt_listener);
ZMK_SUBSCRIPTION(dude_batt, zmk_battery_state_changed);
#endif

/* ----- public init ----- */

int dude_widget_init(struct dude_widget *w, lv_obj_t *parent) {
    the_widget = w;

    /* Physical mount is vertical. Native firmware coords are landscape
     * 160x68; we lay out along the long axis so X=0 maps to the user's
     * "top". The sprite data is pre-rotated 90deg CW in the codegen so
     * the dude renders upright in the user's view. */
    w->obj = parent;
    w->floor_obj = NULL;

    w->sprite = lv_image_create(parent);
    lv_image_set_src(w->sprite, &dude_idle);
    lv_obj_set_style_bg_opa(w->sprite, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(w->sprite, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->sprite, 0, 0);
    lv_obj_set_style_pad_all(w->sprite, 0, 0);
    /* Stack the dude + caption as a vertically centered group in the
     * user's view. Firmware-X is the user-vertical axis; positive X
     * offset = toward user-top. Dude sits above the caption. */
    lv_obj_align(w->sprite, LV_ALIGN_CENTER, 12, 0);

    /* Caption beneath the sprite, rotated 90deg via LVGL style transform.
     * Strip all container chrome so no axis-aligned background box gets
     * painted (mono theme would otherwise draw an opaque rect). */
    w->caption = lv_label_create(parent);
    lv_label_set_text(w->caption, "idle");
    lv_obj_set_style_bg_opa(w->caption, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(w->caption, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->caption, 0, 0);
    lv_obj_set_style_pad_all(w->caption, 0, 0);
    /* Fix the label's size so we know what its bounding box is, then set
     * the pivot to its center so rotation happens in place. */
    lv_obj_set_size(w->caption, 50, 14);
    lv_obj_set_style_text_align(w->caption, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_transform_pivot_x(w->caption, 25, 0);
    lv_obj_set_style_transform_pivot_y(w->caption, 7, 0);
    lv_obj_set_style_transform_rotation(w->caption, 900, 0);
    lv_obj_align(w->caption, LV_ALIGN_CENTER, -38, 0);

    /* Status row at user-TOP (firmware-RIGHT edge). User-RIGHT = battery%,
     * user-LEFT = wifi+tick. Both rotated 90deg. y_offset controls
     * user-horizontal position (firmware-Y); smaller |y_offset| = further
     * from the user-side-edge. */
    static const struct { int pivot_x, pivot_y, w, h, y_offset; const char *initial; } slots[] = {
        { 16, 7, 32, 14, +12, "..%" },                          /* batt → user-right (pad from edge) */
        { 14, 7, 28, 14, -18, LV_SYMBOL_WIFI " " LV_SYMBOL_OK },/* conn → user-left */
    };
    lv_obj_t **targets[] = { &w->batt_label, &w->conn_label };
    for (int i = 0; i < 2; i++) {
        lv_obj_t *l = lv_label_create(parent);
        lv_label_set_text(l, slots[i].initial);
        lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_opa(l, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(l, 0, 0);
        lv_obj_set_style_pad_all(l, 0, 0);
        lv_obj_set_size(l, slots[i].w, slots[i].h);
        /* Left-align so a varying-width trailing glyph (tick/blank)
         * doesn't shift the leading icon. */
        lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_LEFT, 0);
        lv_obj_set_style_transform_pivot_x(l, slots[i].pivot_x, 0);
        lv_obj_set_style_transform_pivot_y(l, slots[i].pivot_y, 0);
        lv_obj_set_style_transform_rotation(l, 900, 0);
        lv_obj_align(l, LV_ALIGN_RIGHT_MID, -2, slots[i].y_offset);
        *targets[i] = l;
    }
    /* Crisp pixel font for the battery percentage; the WIFI/OK symbol
     * stays in Montserrat because Unscii's symbol coverage is limited. */
    lv_obj_set_style_text_font(w->batt_label, &lv_font_unscii_8, 0);

    schedule_tick();
    return 0;
}
