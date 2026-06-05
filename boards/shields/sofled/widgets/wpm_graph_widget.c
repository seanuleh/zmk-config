/*
 * WPM graph widget for the right half.
 *
 * Maintains a ring of WPM samples (one per second) and pushes them into an
 * lv_chart so the bar graph scrolls left-to-right as new samples land.
 * Stats footer shows current / sliding average / session peak WPM.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wpm_graph_widget.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* sliding window for current WPM */
#define KEY_RING_SIZE 64
#define WPM_WINDOW_MS 5000
#define WPM_MULTIPLIER 12

static int64_t key_times_ms[KEY_RING_SIZE];
static uint8_t key_ring_head;

/* graph */
#define CHART_POINTS 56
#define WPM_MAX 120
static uint8_t wpm_peak;
static uint16_t wpm_avg_accum;
static uint16_t wpm_avg_count;

static struct wpm_graph_widget *the_widget;

static uint8_t compute_wpm(int64_t now_ms) {
    int n = 0;
    for (int i = 0; i < KEY_RING_SIZE; i++) {
        int64_t t = key_times_ms[i];
        if (t > 0 && (now_ms - t) <= WPM_WINDOW_MS) n++;
    }
    return (uint8_t)(n * WPM_MULTIPLIER);
}

/* ----- tick: sample once a second, push to chart ----- */

static void wpm_graph_tick(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(wpm_graph_work, wpm_graph_tick);

static void wpm_graph_tick(struct k_work *work) {
    int64_t now = k_uptime_get();
    uint8_t wpm = compute_wpm(now);

    if (wpm > wpm_peak) wpm_peak = wpm;
    wpm_avg_accum += wpm;
    if (wpm_avg_count < 0xFFFE) wpm_avg_count++;
    uint16_t avg = wpm_avg_count ? (wpm_avg_accum / wpm_avg_count) : 0;

    if (the_widget && the_widget->chart && the_widget->series) {
        lv_chart_set_next_value(the_widget->chart, the_widget->series, wpm);
        lv_chart_refresh(the_widget->chart);
    }
    if (the_widget && the_widget->now_label) {
        lv_label_set_text_fmt(the_widget->now_label, "now  %u", wpm);
        lv_label_set_text_fmt(the_widget->avg_label, "avg  %u", avg);
        lv_label_set_text_fmt(the_widget->peak_label, "peak %u", wpm_peak);
    }

    k_work_schedule(&wpm_graph_work, K_SECONDS(1));
}

/* ----- key listener ----- */

static int wpm_graph_key_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev || !ev->state) return 0;
    key_times_ms[key_ring_head] = k_uptime_get();
    key_ring_head = (key_ring_head + 1) % KEY_RING_SIZE;
    return 0;
}
ZMK_LISTENER(wpm_graph_key, wpm_graph_key_listener);
ZMK_SUBSCRIPTION(wpm_graph_key, zmk_position_state_changed);

/* ----- init ----- */

int wpm_graph_widget_init(struct wpm_graph_widget *w, lv_obj_t *parent) {
    the_widget = w;

    /* Container — landscape 160x68 native */
    w->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(w->obj);
    lv_obj_set_size(w->obj, 160, 68);

    /* Header top-left */
    w->header = lv_label_create(w->obj);
    lv_label_set_text(w->header, "wpm");
    lv_obj_align(w->header, LV_ALIGN_TOP_LEFT, 2, 2);

    /* Chart fills the middle band */
    w->chart = lv_chart_create(w->obj);
    lv_obj_set_size(w->chart, 100, 44);
    lv_obj_align(w->chart, LV_ALIGN_TOP_LEFT, 30, 16);
    lv_chart_set_type(w->chart, LV_CHART_TYPE_BAR);
    lv_chart_set_range(w->chart, LV_CHART_AXIS_PRIMARY_Y, 0, WPM_MAX);
    lv_chart_set_point_count(w->chart, CHART_POINTS);
    lv_obj_set_style_pad_all(w->chart, 1, LV_PART_MAIN);
    lv_chart_set_div_line_count(w->chart, 0, 0);
    w->series = lv_chart_add_series(w->chart, lv_color_white(),
                                     LV_CHART_AXIS_PRIMARY_Y);

    /* prime with zeros */
    for (int i = 0; i < CHART_POINTS; i++) {
        lv_chart_set_next_value(w->chart, w->series, 0);
    }

    /* Stats stack on the right */
    w->now_label  = lv_label_create(w->obj);
    w->avg_label  = lv_label_create(w->obj);
    w->peak_label = lv_label_create(w->obj);
    lv_label_set_text(w->now_label,  "now 0");
    lv_label_set_text(w->avg_label,  "avg 0");
    lv_label_set_text(w->peak_label, "pk 0");
    lv_obj_align(w->now_label,  LV_ALIGN_TOP_RIGHT, -2, 16);
    lv_obj_align(w->avg_label,  LV_ALIGN_TOP_RIGHT, -2, 32);
    lv_obj_align(w->peak_label, LV_ALIGN_TOP_RIGHT, -2, 48);

    k_work_schedule(&wpm_graph_work, K_SECONDS(1));
    return 0;
}
