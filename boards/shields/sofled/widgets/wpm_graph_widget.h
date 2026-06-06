/*
 * Right-half widget — WPM history waveform + header/footer stats (portrait).
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <lvgl.h>

struct wpm_graph_widget {
    lv_obj_t *obj;
    lv_obj_t *canvas;
    /* user-top (firmware-RIGHT): batt% + wifi/tick, mirrors left half */
    lv_obj_t *batt_label;
    lv_obj_t *conn_label;
    /* user-bottom (firmware-LEFT): 3 rows. Each row has a fixed name label
     * (user-LEFT, left-aligned) and a number label (user-RIGHT, right-aligned)
     * so changing digit count never moves the visible text. */
    lv_obj_t *footer_name[3];
    lv_obj_t *footer_num[3];
};

int wpm_graph_widget_init(struct wpm_graph_widget *w, lv_obj_t *parent);
