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
#include <zmk/battery.h>

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
}

/* ----- periodic tick ----- */

static void dude_tick_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(dude_tick_work, dude_tick_handler);

static void schedule_tick(void) {
    /* 250ms feels animated enough for the walk cycle without thrashing
     * the Sharp LCD (each frame swap is a visible refresh). */
    k_work_schedule(&dude_tick_work, K_MSEC(250));
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
    /* Kill the mono theme's container chrome (bg + border) which paints
     * a pulsing opaque box around the sprite. Keep the default layout
     * styles intact so the widget auto-sizes to the image. */
    lv_obj_set_style_bg_opa(w->sprite, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(w->sprite, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->sprite, 0, 0);
    lv_obj_set_style_pad_all(w->sprite, 0, 0);
    /* firmware-RIGHT maps to user-TOP on this mount */
    lv_obj_align(w->sprite, LV_ALIGN_RIGHT_MID, -8, 0);

    /* Caption omitted for now — text labels need runtime rotation
     * (lv_obj_set_style_transform_rotation) which I'll wire as polish.
     * The sprite's expression alone telegraphs the current state. */
    w->caption = NULL;

    schedule_tick();
    return 0;
}
