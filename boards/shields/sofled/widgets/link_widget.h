/* SPDX-License-Identifier: MIT */
#pragma once

#include <lvgl.h>

/* Creates the dongle's split-link label (RSSI / drops / last drop). */
lv_obj_t *link_widget_init(lv_obj_t *parent);
