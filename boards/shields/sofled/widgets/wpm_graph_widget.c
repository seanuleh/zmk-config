/*
 * WPM graph widget — right half, portrait layout, lv_canvas waveform.
 *
 * Display is 160×68 firmware (landscape native), mounted vertically.
 * Firmware-X axis = user-vertical (x=0 → user-bottom, x=159 → user-top).
 * Firmware-Y axis = user-horizontal (y=0 → user-left, y=67 → user-right).
 *
 * Single lv_canvas in TRUE_COLOR format. On LV_COLOR_DEPTH=1 the buffer is
 * naturally 1-bit per pixel and lv_canvas_draw_rect blits without per-pixel
 * overhead. One object — no scrollbar/style pressure from 26 lv_obj bars.
 *
 * Step 1: canvas waveform only. Header/footer to come once this boots.
 *
 * SPDX-License-Identifier: MIT
 */

#include "wpm_graph_widget.h"

#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define KEY_RING_SIZE   64
#define WPM_WINDOW_MS   2000
#define WPM_MULTIPLIER  6     /* keys/2s * 30 = chars/min; /5 chars/word → real WPM */
#define WPM_MAX        120

static int64_t key_times_ms[KEY_RING_SIZE];
static uint8_t key_ring_head;

/* CANVAS_W (firmware-x) = amplitude axis (user-vertical bar height).
 * CANVAS_H (firmware-y) = sample axis (user-horizontal time, left→right).
 * Each sample is BAR_PITCH px wide in firmware-y so adjacent bars TOUCH —
 * gives the filled-mountain waveform look from mockup D rather than thin
 * separated lines. ~10px user-top + ~10px user-bottom reserved for upcoming
 * header/footer text. Narrower than max so 32px-wide rotated labels fit
 * on both firmware-x ends (header at user-top, footer at user-bottom). */
#define CANVAS_W 56
#define CANVAS_H 60
#define BAR_PITCH 2
#define N_SAMPLES (CANVAS_H / BAR_PITCH)
#define CANVAS_BPP 1
#define CANVAS_STRIDE 4

static uint8_t wpm_ring[N_SAMPLES];
static uint8_t ring_head;

static uint8_t wpm_peak;
static uint32_t wpm_avg_accum;
static uint16_t wpm_avg_count;
static uint8_t batt_pct = 100;

static uint8_t canvas_buf[LV_CANVAS_BUF_SIZE(CANVAS_W, CANVAS_H, CANVAS_BPP, CANVAS_STRIDE)];

static struct wpm_graph_widget *the_widget;

static uint8_t compute_wpm(int64_t now_ms) {
    int n = 0;
    for (int i = 0; i < KEY_RING_SIZE; i++) {
        int64_t t = key_times_ms[i];
        if (t > 0 && (now_ms - t) <= WPM_WINDOW_MS) n++;
    }
    return (uint8_t)(n * WPM_MULTIPLIER);
}

static void redraw_canvas(struct wpm_graph_widget *w) {
    if (!w->canvas) return;

    /* LVGL 9 layer API: queues all draws onto one layer, dispatches them in
     * one shot at finish_layer, invalidates the canvas exactly once. Avoids
     * the per-set_px invalidate storm that starves BLE. */
    lv_layer_t layer;
    lv_canvas_init_layer(w->canvas, &layer);

    /* Color args are SWAPPED from the obvious — Sharp Memory LCD on this
     * board renders framebuffer-WHITE as panel-on (user sees DARK) and
     * framebuffer-BLACK as panel-off (user sees LIGHT). To get mockup D's
     * white-peaks-on-dark look, fill bg with white and draw bars with black. */
    lv_draw_rect_dsc_t bg_dsc;
    lv_draw_rect_dsc_init(&bg_dsc);
    bg_dsc.bg_color = lv_color_white();
    bg_dsc.bg_opa = LV_OPA_COVER;
    bg_dsc.border_width = 0;
    bg_dsc.radius = 0;
    lv_area_t bg_area = {0, 0, CANVAS_W - 1, CANVAS_H - 1};
    lv_draw_rect(&layer, &bg_dsc, &bg_area);

    lv_draw_rect_dsc_t bar_dsc;
    lv_draw_rect_dsc_init(&bar_dsc);
    bar_dsc.bg_color = lv_color_black();
    bar_dsc.bg_opa = LV_OPA_COVER;
    bar_dsc.border_width = 0;
    bar_dsc.radius = 0;

    for (int i = 0; i < N_SAMPLES; i++) {
        /* Oldest at slot 0 (user-left), newest at last slot (user-right). */
        int idx = (ring_head + i) % N_SAMPLES;
        uint8_t wpm = wpm_ring[idx];
        int bar_len = (int)wpm * CANVAS_W / WPM_MAX;
        if (bar_len > CANVAS_W) bar_len = CANVAS_W;
        if (bar_len <= 0) continue;
        /* Each sample occupies BAR_PITCH rows in firmware-y so adjacent
         * samples abut → filled-mountain look. Bar grows in firmware-x
         * (user-vertical UP) from canvas-x=0 outward. */
        int y0 = i * BAR_PITCH;
        lv_area_t bar = {0, y0, bar_len - 1, y0 + BAR_PITCH - 1};
        lv_draw_rect(&layer, &bar_dsc, &bar);
    }

    lv_canvas_finish_layer(w->canvas, &layer);
}

static void wpm_graph_tick(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(wpm_graph_work, wpm_graph_tick);

static void wpm_graph_tick(struct k_work *work) {
    int64_t now = k_uptime_get();
    uint8_t wpm = compute_wpm(now);

    if (wpm > wpm_peak) wpm_peak = wpm;
    wpm_avg_accum += wpm;
    if (wpm_avg_count < 0xFFFE) wpm_avg_count++;
    uint8_t avg = (uint8_t)(wpm_avg_count ? (wpm_avg_accum / wpm_avg_count) : 0);

    wpm_ring[ring_head] = wpm;
    ring_head = (ring_head + 1) % N_SAMPLES;

    if (the_widget) {
        redraw_canvas(the_widget);
        if (the_widget->footer_num[0])
            lv_label_set_text_fmt(the_widget->footer_num[0], "%3u", wpm);
        if (the_widget->footer_num[1])
            lv_label_set_text_fmt(the_widget->footer_num[1], "%3u", avg);
        if (the_widget->footer_num[2])
            lv_label_set_text_fmt(the_widget->footer_num[2], "%3u", wpm_peak);
        if (the_widget->batt_label) {
            /* Always 3 chars: "100" or " 5%"/"92%". See dude_widget for why. */
            if (batt_pct >= 100)
                lv_label_set_text(the_widget->batt_label, "100");
            else
                lv_label_set_text_fmt(the_widget->batt_label, "%2u%%", batt_pct);
        }
        if (the_widget->conn_label) {
            bool conn = zmk_split_bt_peripheral_is_connected();
            lv_label_set_text(the_widget->conn_label,
                              conn ? LV_SYMBOL_WIFI " " LV_SYMBOL_OK : LV_SYMBOL_WIFI);
        }
    }

    k_work_schedule(&wpm_graph_work, K_SECONDS(1));
}

static int wpm_graph_key_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev || !ev->state) return 0;
    key_times_ms[key_ring_head] = k_uptime_get();
    key_ring_head = (key_ring_head + 1) % KEY_RING_SIZE;
    return 0;
}
ZMK_LISTENER(wpm_graph_key, wpm_graph_key_listener);
ZMK_SUBSCRIPTION(wpm_graph_key, zmk_position_state_changed);

static int wpm_graph_conn_listener(const zmk_event_t *eh) {
    if (the_widget && the_widget->conn_label) {
        bool conn = zmk_split_bt_peripheral_is_connected();
        lv_label_set_text(the_widget->conn_label,
                          conn ? LV_SYMBOL_WIFI " " LV_SYMBOL_OK : LV_SYMBOL_WIFI);
    }
    return 0;
}
ZMK_LISTENER(wpm_graph_conn, wpm_graph_conn_listener);
ZMK_SUBSCRIPTION(wpm_graph_conn, zmk_split_peripheral_status_changed);

#if IS_ENABLED(CONFIG_ZMK_BATTERY_REPORTING)
static int wpm_graph_batt_listener(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    if (ev) batt_pct = ev->state_of_charge;
    return 0;
}
ZMK_LISTENER(wpm_graph_batt, wpm_graph_batt_listener);
ZMK_SUBSCRIPTION(wpm_graph_batt, zmk_battery_state_changed);
#endif

/* Helper: create a label, strip styles, fix size, rotate 90° CW around center.
 * align controls horizontal text alignment within the box BEFORE rotation —
 * after rotation it controls how the text grows along the user-vertical axis:
 * LEFT_ALIGN anchors one edge (good when one end is a fixed icon, varying-width
 * trailing glyph); CENTER keeps the visual center fixed as text length changes. */
static lv_obj_t *make_rot_label(lv_obj_t *parent, int w, int h,
                                lv_text_align_t align, const char *initial) {
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, initial);
    lv_obj_set_style_bg_opa(l, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(l, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(l, 0, 0);
    lv_obj_set_style_pad_all(l, 0, 0);
    lv_obj_set_size(l, w, h);
    lv_obj_set_style_text_align(l, align, 0);
    lv_obj_set_style_transform_pivot_x(l, w / 2, 0);
    lv_obj_set_style_transform_pivot_y(l, h / 2, 0);
    lv_obj_set_style_transform_rotation(l, 900, 0);
    return l;
}

int wpm_graph_widget_init(struct wpm_graph_widget *w, lv_obj_t *parent) {
    the_widget = w;
    w->obj = parent;

    /* Canvas — graph hero, centered, leaving firmware-x margins for the
     * rotated label bands at user-top (header) and user-bottom (footer). */
    w->canvas = lv_canvas_create(parent);
    lv_canvas_set_buffer(w->canvas, canvas_buf, CANVAS_W, CANVAS_H,
                         LV_COLOR_FORMAT_I1);
    /* I1 palette is uninitialized by default → everything renders black.
     * Set index 0 = black, index 1 = white. */
    lv_canvas_set_palette(w->canvas, 0, (lv_color32_t){.blue = 0,   .green = 0,   .red = 0,   .alpha = 0xFF});
    lv_canvas_set_palette(w->canvas, 1, (lv_color32_t){.blue = 255, .green = 255, .red = 255, .alpha = 0xFF});
    lv_obj_remove_style_all(w->canvas);
    lv_obj_align(w->canvas, LV_ALIGN_CENTER, 0, 0);
    lv_canvas_fill_bg(w->canvas, lv_color_white(), LV_OPA_COVER);

    /* Header at user-TOP (firmware-RIGHT edge): mirrors left half.
     * y_offset shifts along firmware-y (user-horizontal): +12 = user-right,
     * -18 = user-left. */
    w->batt_label = make_rot_label(parent, 32, 14, LV_TEXT_ALIGN_LEFT, "..%");
    lv_obj_align(w->batt_label, LV_ALIGN_RIGHT_MID, -2, +12);
    lv_obj_set_style_text_font(w->batt_label, &lv_font_unscii_8, 0);

    w->conn_label = make_rot_label(parent, 28, 14, LV_TEXT_ALIGN_LEFT,
                                   LV_SYMBOL_WIFI " " LV_SYMBOL_OK);
    lv_obj_align(w->conn_label, LV_ALIGN_RIGHT_MID, -2, -18);

    /* Footer at user-BOTTOM: 3 rows stacked along firmware-x. Each row has
     * a separate name label (LEFT-aligned, user-LEFT side via y_offset=-22)
     * and number label (RIGHT-aligned, user-RIGHT side via y_offset=+22).
     * Since the number label uses fixed-width "%3u" with RIGHT alignment in
     * a fixed-size box, the rightmost digit ALWAYS lands at the same firmware
     * position regardless of 1/2/3-digit count — no visible shift. */
    /* Unscii_8 glyphs are 8 px wide. "peak" = 32 px so name boxes need w=32.
     * Number boxes use the same width for consistent visible content fx.
     * y_offsets sized so name (w=32, LEFT-aligned, text up to 32 fy wide)
     * fits at user-LEFT (fy 2..34) and number (RIGHT-aligned, last digit at
     * fy=67) sits at user-RIGHT (fy 44..67) with a clean 9-px gap. */
    static const char *const names[3] = {"now", "avg", "peak"};
    for (int i = 0; i < 3; i++) {
        int x_off = i * 12;
        w->footer_name[i] = make_rot_label(parent, 32, 12,
                                           LV_TEXT_ALIGN_LEFT, names[i]);
        lv_obj_align(w->footer_name[i], LV_ALIGN_LEFT_MID, x_off, -16);
        lv_obj_set_style_text_font(w->footer_name[i], &lv_font_unscii_8, 0);

        w->footer_num[i] = make_rot_label(parent, 32, 12,
                                          LV_TEXT_ALIGN_RIGHT, "  0");
        lv_obj_align(w->footer_num[i], LV_ALIGN_LEFT_MID, x_off, +18);
        lv_obj_set_style_text_font(w->footer_num[i], &lv_font_unscii_8, 0);
    }

    k_work_schedule(&wpm_graph_work, K_SECONDS(1));
    return 0;
}
