/*
 * Right-half widget — live scrolling WPM graph + header/footer stats.
 * Pure local: each half computes WPM from its own keypresses.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <lvgl.h>

struct wpm_graph_widget {
    lv_obj_t *obj;
    lv_obj_t *header;    /* layer/batt — phase 2: stub text */
    lv_obj_t *chart;     /* lv_chart */
    lv_chart_series_t *series;
    lv_obj_t *now_label;
    lv_obj_t *avg_label;
    lv_obj_t *peak_label;
};

int wpm_graph_widget_init(struct wpm_graph_widget *w, lv_obj_t *parent);
