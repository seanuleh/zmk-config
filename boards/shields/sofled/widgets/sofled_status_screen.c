/*
 * Sofle portrait custom status screen.
 *
 * Rotates the display 90deg so we author in portrait coords (68x160), then
 * mounts the appropriate widget for this half:
 *   - left  : animated Claude critter (dude_widget)
 *   - right : live WPM graph (wpm_graph_widget)
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_SHIELD_SOFLED_LEFT)
#include "dude_widget.h"
static struct dude_widget left_widget;
#endif

#if IS_ENABLED(CONFIG_SHIELD_SOFLED_RIGHT)
#include "wpm_graph_widget.h"
static struct wpm_graph_widget right_widget;
#endif

static void apply_portrait_rotation(void) {
    lv_disp_t *disp = lv_disp_get_default();
    if (disp != NULL) {
        lv_disp_set_rotation(disp, LV_DISP_ROTATION_90);
    }
}

lv_obj_t *zmk_display_status_screen(void) {
    apply_portrait_rotation();

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);

#if IS_ENABLED(CONFIG_SHIELD_SOFLED_LEFT)
    dude_widget_init(&left_widget, screen);
    lv_obj_align(left_widget.obj, LV_ALIGN_TOP_MID, 0, 0);
#elif IS_ENABLED(CONFIG_SHIELD_SOFLED_RIGHT)
    wpm_graph_widget_init(&right_widget, screen);
    lv_obj_align(right_widget.obj, LV_ALIGN_TOP_MID, 0, 0);
#else
    /* Fallback stub for the dongle if its display is ever enabled */
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "sofle");
    lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    lv_obj_center(label);
#endif

    return screen;
}
