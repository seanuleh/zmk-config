/*
 * Dongle status screen (SSD1306 128x32). Same as ZMK's built-in screen
 * (output top-left, layer bottom-left) with the battery widget replaced by
 * the split-link panel on the right.
 *
 * SPDX-License-Identifier: MIT
 */

#include <lvgl.h>
#include <zmk/display/status_screen.h>
#include <zmk/display/widgets/output_status.h>
#include <zmk/display/widgets/layer_status.h>

#include "link_widget.h"

static struct zmk_widget_output_status output_status_widget;
static struct zmk_widget_layer_status layer_status_widget;

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);

    zmk_widget_output_status_init(&output_status_widget, screen);
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget), LV_ALIGN_TOP_LEFT, 0, 0);

    zmk_widget_layer_status_init(&layer_status_widget, screen);
    lv_obj_set_style_text_font(zmk_widget_layer_status_obj(&layer_status_widget),
                               lv_theme_get_font_small(screen), LV_PART_MAIN);
    lv_obj_align(zmk_widget_layer_status_obj(&layer_status_widget), LV_ALIGN_BOTTOM_LEFT, 0, 0);

    lv_obj_align(link_widget_init(screen), LV_ALIGN_TOP_RIGHT, 0, 0);

    return screen;
}
