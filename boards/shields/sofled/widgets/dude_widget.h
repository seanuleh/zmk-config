/*
 * Claude critter "dude" widget — animated sprite reacting to local typing.
 * Used on the left half. State machine driven by local keypresses + battery.
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <lvgl.h>

struct dude_widget {
    lv_obj_t *obj;        /* container */
    lv_obj_t *sprite;     /* lv_image showing current frame */
    lv_obj_t *floor_obj;  /* dotted floor line */
    lv_obj_t *caption;    /* small state label below */
    lv_obj_t *batt_label; /* battery % at user-top */
    lv_obj_t *conn_label; /* BLE peripheral conn status at user-top */
};

int dude_widget_init(struct dude_widget *w, lv_obj_t *parent);
